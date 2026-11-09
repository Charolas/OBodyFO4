#pragma once

// Presets loaded at startup, kept in memory for the in-game preset menu and ORefit.

namespace OBodyFO4 {

    struct Slider {
        std::string name;
        float value;
    };

    // ORefit change for one slider: added to the actor's current value, or (absolute)
    // the slider is set to value.
    struct RefitDelta {
        std::string name;
        float value;
        bool absolute;
    };

    struct Preset {
        std::string name;    // preset name as shown in BodySlide
        std::string marker;  // marker morph that tags an actor with this preset
        std::vector<Slider> sliders;
        std::vector<RefitDelta> refitDeltas;  // ORefit for this preset
    };

    // ORefit for actors without one of our presets (the player with a hand-made body).
    std::vector<RefitDelta>& GetGenericRefit(bool a_female);

    // Marker morph: exists in no .tri file (does not change the body), but is
    // saved on the actor and tells which preset it received.
    inline constexpr auto kMarkerPrefix = "OBodyFO4Preset_"sv;

    // Marker morph set while ORefit is applied.
    inline constexpr auto kRefitMarker = "OBodyFO4Refit"sv;

    // ORefit never overwrites the body: for every slider it changes it stores the
    // exact change in "OBodyFO4Delta_<slider>" and undoes it later (value - delta).
    // A zero change is not stored (LooksMenu drops zero-valued morphs anyway).
    inline constexpr auto kDeltaPrefix = "OBodyFO4Delta_"sv;

    // index 0 = male (BodyTalk), 1 = female (active female body)
    std::vector<Preset>& GetPresets(bool a_female);

    // ORefit outfit lists from every JSON in Data\F4SE\Plugins\OBodyFO4 (union of all files).
    struct RefitRules {
        std::vector<std::pair<std::string, std::uint32_t>> blacklistFormIDs;  // plugin, local id
        std::vector<std::pair<std::string, std::uint32_t>> forceFormIDs;
        std::unordered_set<std::string> blacklistNames;    // lowercase armor names
        std::unordered_set<std::string> blacklistPlugins;  // lowercase plugin names
        std::unordered_set<std::string> forceNames;
        std::unordered_set<int>         extraSlots;  // "orefitBodySlots": extra slot numbers (30-61)
    };
    RefitRules& GetRefitRules();

    void RegisterPapyrus();
    void HookCrosshair();
    void SpeedUpBodyGen();
    void InstallRefit();  // equip event sink + resolves the JSON FormIDs (call at kGameDataReady)
    std::int32_t IsClothedImpl(RE::Actor* a_actor);  // 1 clothed, 0 naked, -1 unknown
    void RefitDoneImpl(std::uint32_t a_formID);

}  // namespace OBodyFO4
