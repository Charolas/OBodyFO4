// ORefit: when an NPC wears clothes on the body slots, switch to the clothed
// version of its preset (push-up, smoother shapes); back to the nude preset when naked.
//
// C++ only watches TESEquipEvent and filters cheaply (human actor, armor on
// slot 33 BODY or 36 [U] Torso). The morphs are applied by Papyrus
// (OBodyFO4.UpdateRefit) through LooksMenu's BodyGen script, like the menu.
// The actor goes to Papyrus as a FormID (see GetTargetActorID in Papyrus.cpp).

#include "Presets.h"

namespace OBodyFO4 {

    RefitRules& GetRefitRules() {
        static RefitRules rules;
        return rules;
    }

    namespace {

        // Chest-covering slots that count as "clothed": 33 BODY, 36 [U] Torso, 41 [A] Torso
        // (outfit mods often put tops on 41). Bit = slot - 30. More slots can be added
        // with "orefitBodySlots" in the JSON files. Bottom-only slots (e.g. 40 [U] R Leg)
        // are left out on purpose: a topless actor must not get the push-up.
        std::uint32_t bodySlotsMask = (1u << 3) | (1u << 6) | (1u << 11);

        std::unordered_set<std::uint32_t> blacklistIDs;  // resolved runtime FormIDs
        std::unordered_set<std::uint32_t> forceIDs;

        std::mutex                        pendingLock;
        std::unordered_set<std::uint32_t> pending;  // actors with an UpdateRefit call in flight

        std::string Lower(std::string_view a_text) {
            std::string s{ a_text };
            std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        bool IsHuman(RE::Actor* a_actor) {
            const auto* npc = a_actor ? a_actor->GetNPC() : nullptr;
            const auto* race = npc ? npc->GetFormRace() : nullptr;
            return race && std::string_view{ race->GetFormEditorID() } == "HumanRace"sv;
        }

        bool IsBlacklisted(RE::TESForm* a_armor) {
            auto& rules = GetRefitRules();
            if (blacklistIDs.contains(a_armor->GetFormID())) return true;
            if (!rules.blacklistNames.empty()) {
                if (rules.blacklistNames.contains(Lower(RE::TESFullName::GetFullName(*a_armor)))) return true;
            }
            if (!rules.blacklistPlugins.empty()) {
                if (const auto* file = a_armor->GetFile(0)) {
                    if (rules.blacklistPlugins.contains(Lower(file->GetFilename()))) return true;
                }
            }
            return false;
        }

        bool IsForced(RE::TESForm* a_armor) {
            auto& rules = GetRefitRules();
            if (forceIDs.contains(a_armor->GetFormID())) return true;
            return !rules.forceNames.empty() && rules.forceNames.contains(Lower(RE::TESFullName::GetFullName(*a_armor)));
        }

        class EquipSink final : public RE::BSTEventSink<RE::TESEquipEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent& a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override {
                auto* actor = a_event.actor ? a_event.actor->As<RE::Actor>() : nullptr;
                if (!actor || !IsHuman(actor)) return RE::BSEventNotifyControl::kContinue;

                auto* item = RE::TESForm::GetFormByID(a_event.baseObject);
                if (!item || item->GetFormType() != RE::ENUM_FORM_ID::kARMO) return RE::BSEventNotifyControl::kContinue;
                if ((item->GetFilledSlots() & bodySlotsMask) == 0 && !IsForced(item)) return RE::BSEventNotifyControl::kContinue;

                const auto formID = actor->GetFormID();
                {
                    std::scoped_lock l{ pendingLock };
                    if (!pending.insert(formID).second) return RE::BSEventNotifyControl::kContinue;  // already queued
                }

                const auto game = RE::GameVM::GetSingleton();
                const auto vm = game ? game->GetVM() : nullptr;
                if (!vm || !vm->DispatchStaticCall(RE::BSFixedString{ "OBodyFO4" }, RE::BSFixedString{ "UpdateRefit" }, RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>{}, static_cast<std::int32_t>(formID))) {
                    std::scoped_lock l{ pendingLock };
                    pending.erase(formID);
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        void ResolveFormIDs() {
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data) return;
            auto& rules = GetRefitRules();
            auto resolve = [&](const auto& a_list, std::unordered_set<std::uint32_t>& a_out, std::string_view a_what) {
                for (const auto& [plugin, id] : a_list) {
                    if (auto* form = data->LookupForm(id, plugin)) {
                        a_out.insert(form->GetFormID());
                    } else {
                        REX::WARN("ORefit: {} {}|{:06X} not found (plugin not loaded?)", a_what, plugin, id);
                    }
                }
            };
            resolve(rules.blacklistFormIDs, blacklistIDs, "blacklisted outfit");
            resolve(rules.forceFormIDs, forceIDs, "force-refit outfit");
            REX::INFO("ORefit: {} outfits blacklisted by FormID, {} by name, {} plugins; {} force-refit",
                blacklistIDs.size(), rules.blacklistNames.size(), rules.blacklistPlugins.size(), forceIDs.size() + rules.forceNames.size());
        }

    }  // namespace

    // ------------------------------------------------------------ used by Papyrus.cpp

    // A naked actor "wears" its skin: an ARMO on the body slot (NPC or race skin,
    // e.g. SkinNaked). That is not clothing.
    bool IsSkin(RE::Actor* a_actor, RE::TESForm* a_form) {
        auto* npc = a_actor->GetNPC();
        if (npc && npc->formSkin == a_form) return true;
        const auto* race = npc ? npc->GetFormRace() : nullptr;
        if (race && race->formSkin == a_form) return true;
        return a_actor->race && a_actor->race->formSkin == a_form;
    }

    // 1 = clothed, 0 = naked, -1 = unknown (3D not loaded yet)
    std::int32_t IsClothedImpl(RE::Actor* a_actor) {
        auto* biped = a_actor ? a_actor->biped.get() : nullptr;
        if (!biped) return -1;
        bool clothed = false;
        for (std::size_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kEditorTotal); ++i) {
            auto* form = biped->object[i].parent.object;
            if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kARMO || IsSkin(a_actor, form)) continue;
            if (IsForced(form)) return 1;
            const auto slot = static_cast<RE::BIPED_OBJECT>(i);
            if ((bodySlotsMask & (1u << std::to_underlying(slot))) != 0 && !IsBlacklisted(form)) {
                clothed = true;
            }
        }
        return clothed ? 1 : 0;
    }

    void RefitDoneImpl(std::uint32_t a_formID) {
        std::scoped_lock l{ pendingLock };
        pending.erase(a_formID);
    }

    void InstallRefit() {
        for (const auto slot : GetRefitRules().extraSlots) {
            if (slot >= 30 && slot <= 61) bodySlotsMask |= 1u << (slot - 30);
            else REX::WARN("ORefit: orefitBodySlots value {} ignored (slots go from 30 to 61)", slot);
        }
        std::string slots;
        for (int i = 0; i < 32; ++i) {
            if (bodySlotsMask & (1u << i)) slots += std::format("{}{}", slots.empty() ? "" : ", ", i + 30);
        }
        REX::INFO("ORefit: clothing on slots {} counts as clothed", slots);
        ResolveFormIDs();
        if (auto* source = RE::TESEquipEvent::GetEventSource()) {
            static EquipSink sink;
            source->RegisterSink(&sink);
            REX::INFO("ORefit: equip event sink registered");
        } else {
            REX::ERROR("ORefit: TESEquipEvent source not found, ORefit disabled");
        }
    }

}  // namespace OBodyFO4
