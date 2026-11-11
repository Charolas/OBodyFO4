// Papyrus natives used by OBodyFO4.psc (in-game preset menu).
// Papyrus applies the morphs itself through LooksMenu's BodyGen script:
// calling the VM from C++ crashed (Fallout4.exe+21173A0), Papyrus -> native -> Papyrus does not.

#include "Presets.h"

namespace OBodyFO4 {

    std::vector<Preset>& GetPresets(bool a_female) {
        static std::vector<Preset> lists[2];
        return lists[a_female ? 1 : 0];
    }

    std::vector<RefitDelta>& GetGenericRefit(bool a_female) {
        static std::vector<RefitDelta> lists[2];
        return lists[a_female ? 1 : 0];
    }

    namespace {

        // ------------------------------------------------------------ crosshair
        // Same source LL FourPlay uses for LastCrossHairRef (code from kassent's HUD++):
        // BSTGlobalEvent::EventSource<ViewCasterUpdateEvent>. The first member of the
        // event is the handle of the reference under the crosshair.
        struct ViewCasterUpdateEvent {
            RE::ObjectRefHandle crosshairRef;  // 00
            std::uint8_t        unk04[0x3C];   // rest of the event, unused
        };
        static_assert(sizeof(ViewCasterUpdateEvent) == 0x40);

        std::mutex                            crosshairLock;
        RE::ObjectRefHandle                   crosshairRef;   // what is under the crosshair right now (can be empty)
        RE::ObjectRefHandle                   lastActorRef;   // last actor the crosshair was on
        std::chrono::steady_clock::time_point lastActorTime;
        bool                                  crosshairHooked = false;

        // The crosshair ref flickers back to empty on living (moving) NPCs, which is
        // why LL FourPlay also keeps a "LastCrossHairActor". We do the same.
        class CrosshairSink final : public RE::BSTEventSink<ViewCasterUpdateEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(const ViewCasterUpdateEvent& a_event, RE::BSTEventSource<ViewCasterUpdateEvent>*) override {
                const auto ref = a_event.crosshairRef ? a_event.crosshairRef.get() : RE::NiPointer<RE::TESObjectREFR>{};
                const bool isActor = ref && ref->As<RE::Actor>();
                std::scoped_lock l{ crosshairLock };
                crosshairRef = a_event.crosshairRef;
                if (isActor) {
                    lastActorRef = a_event.crosshairRef;
                    lastActorTime = std::chrono::steady_clock::now();
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        // Every BSTGlobalEvent::EventSource<T> registers itself on the global
        // KillSDM source, so we walk that list and pick ours by RTTI.
        RE::BSTEventSource<ViewCasterUpdateEvent>* FindViewCasterSource() {
            auto* global = RE::BSTGlobalEvent::GetSingleton();
            if (!global) return nullptr;

            using KillSink = RE::BSTEventSink<RE::BSTGlobalEvent::KillSDMEvent>;
            struct SourceView {  // same layout as BSTEventSource (its members are private)
                RE::BSSpinLock         lock;
                RE::BSTArray<KillSink*> sinks;
            };
            const auto* view = reinterpret_cast<const SourceView*>(&global->eventSourceSDMKiller);

            const REL::Relocation<void*> wanted{ RE::RTTI::BSTGlobalEvent__EventSource_ViewCasterUpdateEvent_ };
            for (auto* sink : view->sinks) {
                if (!sink) continue;
                auto** vtbl = *reinterpret_cast<void***>(sink);
                const auto* col = static_cast<const RE::RTTI::CompleteObjectLocator*>(vtbl[-1]);
                if (col && static_cast<void*>(col->typeDescriptor.get()) == wanted.get()) {
                    return static_cast<RE::BSTGlobalEvent::EventSource<ViewCasterUpdateEvent>*>(sink);
                }
            }
            return nullptr;
        }

        // ------------------------------------------------------------ helpers

        const Preset* At(bool a_female, std::int32_t a_index) {
            const auto& list = GetPresets(a_female);
            if (a_index < 0 || static_cast<std::size_t>(a_index) >= list.size()) return nullptr;
            return &list[static_cast<std::size_t>(a_index)];
        }

        // Same rule as the distribution: HumanRace plus "additionalRaces" (children never).
        // Papyrus side declares these as ObjectReference: commonlibf4's RE::Actor has no
        // TYPE_ID of its own, so an RE::Actor* parameter registers as ObjectReference
        // and a Papyrus "Actor" signature is rejected ("does not match existing signature").
        RE::Actor* AsActor(RE::TESObjectREFR* a_ref) { return a_ref ? a_ref->As<RE::Actor>() : nullptr; }

        bool CanReceivePreset(std::monostate, RE::TESObjectREFR* a_ref) {
            auto* a_actor = AsActor(a_ref);
            if (!a_actor) return false;
            const auto* npc = a_actor->GetNPC();
            const auto* race = npc ? npc->GetFormRace() : nullptr;
            if (IsAllowedRace(race)) return true;
            // tells the user which editor ID to put in "additionalRaces"
            REX::INFO("menu: {:08X} has race '{}', not in the allowed races (add it to \"additionalRaces\" in a JSON file if it is an adult human-bodied race)",
                a_actor->GetFormID(), race ? race->GetFormEditorID() : "none");
            return false;
        }

        // Closest angle to the player's heading inside a cone. Fallback when the
        // crosshair gives nothing.
        RE::Actor* GetActorInFront(float a_degrees, float a_maxDist) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* lists = RE::ProcessLists::GetSingleton();
            if (!player || !lists) return nullptr;

            const auto origin = player->GetPosition();
            const float heading = player->data.angle.z;
            const float fx = std::sin(heading);
            const float fy = std::cos(heading);
            const float kMaxDist = a_maxDist;
            const float kMinCos = std::cos(a_degrees * 3.14159265f / 180.0f);

            RE::Actor* best = nullptr;
            float bestCos = kMinCos;
            for (const auto& handle : lists->highActorHandles) {
                const auto ptr = handle.get();
                auto* actor = ptr.get();
                if (!actor || actor == player || actor->IsDisabled()) continue;
                const auto pos = actor->GetPosition();
                const float dx = pos.x - origin.x;
                const float dy = pos.y - origin.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < 1.0f || dist > kMaxDist) continue;
                const float c = (dx * fx + dy * fy) / dist;
                if (c > bestCos) {
                    bestCos = c;
                    best = actor;
                }
            }
            return best;
        }

        RE::Actor* Resolve(const RE::ObjectRefHandle& a_handle) {
            if (!a_handle) return nullptr;
            const auto ref = a_handle.get();
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            return actor && !actor->IsDisabled() ? actor : nullptr;
        }

        // Actor under the crosshair. None = player.
        // 1) actor under the crosshair now, 2) last actor the crosshair was on in the
        // last 2 s, 3) actor straight ahead (10 degrees, 1000 units).
        RE::Actor* FindTarget() {
            RE::ObjectRefHandle now, last;
            std::chrono::steady_clock::time_point lastTime;
            {
                std::scoped_lock l{ crosshairLock };
                now = crosshairRef;
                last = lastActorRef;
                lastTime = lastActorTime;
            }
            if (crosshairHooked) {
                if (auto* actor = Resolve(now)) {
                    REX::INFO("menu: target from crosshair");
                    return actor;
                }
                if (std::chrono::steady_clock::now() - lastTime < 2s) {
                    if (auto* actor = Resolve(last)) {
                        REX::INFO("menu: target from recent crosshair");
                        return actor;
                    }
                }
            }
            if (auto* actor = GetActorInFront(10.0f, 1000.0f)) {
                REX::INFO("menu: target from actor in front");
                return actor;
            }
            REX::INFO("menu: no target, using the player");
            return nullptr;
        }

        // Returned as a FormID, not as a form: commonlibf4 packs an RE::Actor* as an
        // ObjectReference, and for actors that already have scripts bound (any living
        // NPC) that came back as None in Papyrus. Papyrus does Game.GetForm(id) as Actor.
        std::int32_t GetTargetActorID(std::monostate) {
            auto* actor = FindTarget();
            return actor ? static_cast<std::int32_t>(actor->GetFormID()) : 0;
        }

        // Only one switch at a time: pressing again mid-apply mixed two presets.
        // The 5 s timeout frees the lock if a script ever dies before EndApply.
        std::mutex                            applyLock;
        bool                                  applying = false;
        std::chrono::steady_clock::time_point applyStart;

        bool BeginApply(std::monostate) {
            std::scoped_lock l{ applyLock };
            const auto now = std::chrono::steady_clock::now();
            if (applying && now - applyStart < 5s) {
                REX::INFO("menu: hotkey ignored, previous call still running");
                return false;
            }
            REX::INFO("menu: hotkey received");
            applying = true;
            applyStart = now;
            return true;
        }

        void EndApply(std::monostate) {
            REX::INFO("menu: done");
            std::scoped_lock l{ applyLock };
            applying = false;
        }

        RE::BSFixedString GetActorName(std::monostate, RE::TESObjectREFR* a_actor) {
            const char* name = a_actor ? a_actor->GetDisplayFullName() : nullptr;
            REX::INFO("menu: target {} ({:08X})", name ? name : "?", a_actor ? a_actor->GetFormID() : 0);
            return RE::BSFixedString{ name && *name ? name : "Unknown" };
        }

        // All preset names in one string, "\n"-separated, for the menu list.
        RE::BSFixedString GetPresetNamesJoined(std::monostate, bool a_female) {
            REX::INFO("menu: open, sending {} {} presets to the menu", GetPresets(a_female).size(), a_female ? "female" : "male");
            std::string out;
            for (const auto& p : GetPresets(a_female)) {
                if (!out.empty()) out += '\n';
                out += p.name;
            }
            return RE::BSFixedString{ out.c_str() };
        }

        std::int32_t GetPresetCount(std::monostate, bool a_female) {
            return static_cast<std::int32_t>(GetPresets(a_female).size());
        }

        RE::BSFixedString GetPresetName(std::monostate, bool a_female, std::int32_t a_preset) {
            const auto* p = At(a_female, a_preset);
            return p ? RE::BSFixedString{ p->name.c_str() } : RE::BSFixedString{};
        }

        RE::BSFixedString GetMarkerName(std::monostate, bool a_female, std::int32_t a_preset) {
            const auto* p = At(a_female, a_preset);
            return p ? RE::BSFixedString{ p->marker.c_str() } : RE::BSFixedString{};
        }

        std::int32_t GetSliderCount(std::monostate, bool a_female, std::int32_t a_preset) {
            const auto* p = At(a_female, a_preset);
            return p ? static_cast<std::int32_t>(p->sliders.size()) : 0;
        }

        RE::BSFixedString GetSliderName(std::monostate, bool a_female, std::int32_t a_preset, std::int32_t a_slider) {
            const auto* p = At(a_female, a_preset);
            if (!p || a_slider < 0 || static_cast<std::size_t>(a_slider) >= p->sliders.size()) return {};
            return RE::BSFixedString{ p->sliders[static_cast<std::size_t>(a_slider)].name.c_str() };
        }

        float GetSliderValue(std::monostate, bool a_female, std::int32_t a_preset, std::int32_t a_slider) {
            const auto* p = At(a_female, a_preset);
            if (!p || a_slider < 0 || static_cast<std::size_t>(a_slider) >= p->sliders.size()) return 0.0f;
            return p->sliders[static_cast<std::size_t>(a_slider)].value;
        }

        // ---- ORefit
        std::int32_t IsClothed(std::monostate, RE::TESObjectREFR* a_ref) { return IsClothedImpl(AsActor(a_ref)); }
        void RefitDone(std::monostate, std::int32_t a_formID) { RefitDoneImpl(static_cast<std::uint32_t>(a_formID)); }

        // Log line so ORefit switches can be checked without eyeballing the body.
        void LogRefit(std::monostate, RE::TESObjectREFR* a_ref, RE::BSFixedString a_preset, bool a_clothed) {
            const char* name = a_ref ? a_ref->GetDisplayFullName() : nullptr;
            REX::INFO("ORefit: {} ({:08X}) -> {} version of '{}'", name ? name : "?", a_ref ? a_ref->GetFormID() : 0,
                a_clothed ? "clothed" : "nude", a_preset.c_str());
        }

        // preset -1 = generic ORefit (actor without one of our presets)
        const std::vector<RefitDelta>* Deltas(bool a_female, std::int32_t a_preset) {
            if (a_preset < 0) return &GetGenericRefit(a_female);
            const auto* p = At(a_female, a_preset);
            return p ? &p->refitDeltas : nullptr;
        }

        const RefitDelta* DeltaAt(bool a_female, std::int32_t a_preset, std::int32_t a_index) {
            const auto* list = Deltas(a_female, a_preset);
            if (!list || a_index < 0 || static_cast<std::size_t>(a_index) >= list->size()) return nullptr;
            return &(*list)[static_cast<std::size_t>(a_index)];
        }

        std::int32_t GetRefitDeltaCount(std::monostate, bool a_female, std::int32_t a_preset) {
            const auto* list = Deltas(a_female, a_preset);
            return list ? static_cast<std::int32_t>(list->size()) : 0;
        }

        RE::BSFixedString GetRefitDeltaName(std::monostate, bool a_female, std::int32_t a_preset, std::int32_t a_index) {
            const auto* d = DeltaAt(a_female, a_preset, a_index);
            return d ? RE::BSFixedString{ d->name.c_str() } : RE::BSFixedString{};
        }

        float GetRefitDeltaValue(std::monostate, bool a_female, std::int32_t a_preset, std::int32_t a_index) {
            const auto* d = DeltaAt(a_female, a_preset, a_index);
            return d ? d->value : 0.0f;
        }

        bool IsRefitDeltaAbsolute(std::monostate, bool a_female, std::int32_t a_preset, std::int32_t a_index) {
            const auto* d = DeltaAt(a_female, a_preset, a_index);
            return d && d->absolute;
        }

        // "OBodyFO4Delta_Boobs Big" -> "Boobs Big"; anything else -> ""
        RE::BSFixedString DeltaTarget(std::monostate, RE::BSFixedString a_morph) {
            const std::string_view morph{ a_morph.c_str() };
            if (!morph.starts_with(kDeltaPrefix)) return {};
            return RE::BSFixedString{ std::string{ morph.substr(kDeltaPrefix.size()) }.c_str() };
        }

        bool IsPlayer(std::monostate, std::int32_t a_formID) { return a_formID == 0x14; }

        // Morph name -> preset index, or -1 if it is not one of our markers.
        std::int32_t PresetIndexFromMorph(std::monostate, bool a_female, RE::BSFixedString a_morph) {
            const std::string_view morph{ a_morph.c_str() };
            if (!morph.starts_with(kMarkerPrefix)) return -1;
            const auto& list = GetPresets(a_female);
            for (std::size_t i = 0; i < list.size(); ++i) {
                if (list[i].marker == morph) return static_cast<std::int32_t>(i);
            }
            return -1;
        }

        bool Register(RE::BSScript::IVirtualMachine* a_vm) {
            // tasklet-callable = runs right away on the Papyrus thread instead of
            // waiting for a frame sync on every call (they only read our own data).
            constexpr auto script = "OBodyFO4"sv;
            a_vm->BindNativeMethod(script, "CanReceivePreset"sv, CanReceivePreset, true);
            a_vm->BindNativeMethod(script, "GetTargetActorID"sv, GetTargetActorID, true);
            a_vm->BindNativeMethod(script, "BeginApply"sv, BeginApply, true);
            a_vm->BindNativeMethod(script, "EndApply"sv, EndApply, true);
            a_vm->BindNativeMethod(script, "GetPresetCount"sv, GetPresetCount, true);
            a_vm->BindNativeMethod(script, "GetPresetName"sv, GetPresetName, true);
            a_vm->BindNativeMethod(script, "GetPresetNamesJoined"sv, GetPresetNamesJoined, true);
            a_vm->BindNativeMethod(script, "GetActorName"sv, GetActorName);
            a_vm->BindNativeMethod(script, "GetMarkerName"sv, GetMarkerName, true);
            a_vm->BindNativeMethod(script, "GetSliderCount"sv, GetSliderCount, true);
            a_vm->BindNativeMethod(script, "GetSliderName"sv, GetSliderName, true);
            a_vm->BindNativeMethod(script, "GetSliderValue"sv, GetSliderValue, true);
            a_vm->BindNativeMethod(script, "PresetIndexFromMorph"sv, PresetIndexFromMorph, true);
            a_vm->BindNativeMethod(script, "IsClothed"sv, IsClothed);  // reads the actor's 3D: main thread
            a_vm->BindNativeMethod(script, "RefitDone"sv, RefitDone, true);
            a_vm->BindNativeMethod(script, "LogRefit"sv, LogRefit);
            a_vm->BindNativeMethod(script, "GetRefitDeltaCount"sv, GetRefitDeltaCount, true);
            a_vm->BindNativeMethod(script, "GetRefitDeltaName"sv, GetRefitDeltaName, true);
            a_vm->BindNativeMethod(script, "GetRefitDeltaValue"sv, GetRefitDeltaValue, true);
            a_vm->BindNativeMethod(script, "IsRefitDeltaAbsolute"sv, IsRefitDeltaAbsolute, true);
            a_vm->BindNativeMethod(script, "DeltaTarget"sv, DeltaTarget, true);
            a_vm->BindNativeMethod(script, "IsPlayer"sv, IsPlayer, true);
            REX::INFO("Papyrus functions registered");
            return true;
        }

    }  // namespace

    void HookCrosshair() {
        if (crosshairHooked) return;
        if (auto* source = FindViewCasterSource()) {
            static CrosshairSink sink;
            source->RegisterSink(&sink);
            crosshairHooked = true;
            REX::INFO("crosshair: hooked ViewCasterUpdateEvent");
        } else {
            REX::WARN("crosshair: ViewCasterUpdateEvent source not found yet, using the actor in front of the player");
        }
    }

    // LooksMenu's BodyGen natives wait for a frame sync on every call, so a
    // 40-slider preset took ~40 frames. These three only touch LooksMenu's morph
    // map, which it guards with its own lock, so they can run on the Papyrus
    // thread. UpdateMorphs (touches 3D) is left as is.
    void SpeedUpBodyGen() {
        const auto game = RE::GameVM::GetSingleton();
        const auto vm = game ? game->GetVM() : nullptr;
        if (!vm) return;
        for (const auto* fn : { "SetMorph", "GetMorphs", "RemoveMorphsByKeyword" }) {
            vm->SetCallableFromTasklets("BodyGen", fn, true);
        }
        REX::INFO("BodyGen SetMorph/GetMorphs/RemoveMorphsByKeyword set callable from tasklets");
    }

    void RegisterPapyrus() {
        if (const auto papyrus = F4SE::GetPapyrusInterface()) {
            papyrus->Register(Register);
        } else {
            REX::ERROR("Papyrus interface unavailable: in-game preset switcher disabled");
        }
    }

}  // namespace OBodyFO4
