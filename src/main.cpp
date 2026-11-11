// OBodyFO4
// Reads BodySlide presets (Data\Tools\BodySlide\SliderPresets) plus JSON rules
// (Data\F4SE\Plugins\OBodyFO4\*.json) and writes LooksMenu BodyGen files:
//   Data\F4SE\Plugins\F4EE\BodyGen\Loose\OBodyFO4_templates.ini
//   Data\F4SE\Plugins\F4EE\BodyGen\Loose\OBodyFO4_morphs.ini
// Runs once at F4SE load. LooksMenu only reads those files at kGameDataReady,
// so they already exist when it looks. LooksMenu itself rolls, applies, saves
// to its cosave and never re-rolls, all in C++.
//
// Settings (MCM ModSettings):
//   Data\MCM\Config\OBodyFO4\settings.ini   -> mod defaults
//   Data\MCM\Settings\OBodyFO4.ini          -> user changes from the MCM (take priority)

#include "Presets.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace OBodyFO4;

namespace {

    constexpr auto kPresetDir = "Data/Tools/BodySlide/SliderPresets"sv;
    constexpr auto kRulesDir = "Data/F4SE/Plugins/OBodyFO4"sv;
    constexpr auto kOutDir = "Data/F4SE/Plugins/F4EE/BodyGen/Loose"sv;
    constexpr auto kTemplatesFile = "OBodyFO4_templates.ini"sv;
    constexpr auto kMorphsFile = "OBodyFO4_morphs.ini"sv;
    constexpr auto kMcmUserIni = "Data/MCM/Settings/OBodyFO4.ini"sv;
    constexpr auto kMcmDefaultIni = "Data/MCM/Config/OBodyFO4/settings.ini"sv;
    constexpr auto kLooksMenuIni = "Data/F4SE/Plugins/f4ee.ini"sv;
    constexpr auto kLooksMenuCustomIni = "Data/F4SE/Plugins/f4ee_custom.ini"sv;

    // Template with no morphs: blacklisted NPCs/races get this and stay untouched.
    constexpr auto kBlacklistTemplate = "OBodyFO4_Blacklisted"sv;

    // .xml files shipped with the bodies themselves (file name, lowercase, no .xml).
    const std::unordered_set<std::string> kFusionGirlBuiltin{
        "fusion-athletic", "fusion-curvy", "fusion-petite", "fusion-sevenbase", "fusion-vanillaesque"
    };
    const std::unordered_set<std::string> kBodyTalkBuiltin{
        "bt-average", "bt-bodybuilder", "bt-chubby", "bt-muscular", "bt-swimmer", "bt-thin"
    };
    // CBBE ships every stock preset in one CBBE.xml (TWB ships the same file again).
    const std::unordered_set<std::string> kCBBEBuiltin{ "cbbe" };
    // True Wasteland Body (SliderPresets folder of the TWB mod; TWB 3BBB ships none).
    const std::unordered_set<std::string> kTWBBuiltin{
        ".aaab", ".aaagan", ".aaalt", ".abeefy", ".abigiron", ".acoke please", ".acomeonandgetyourlog",
        ".adreams", ".amazongoddess", ".anewbirth", ".anon", ".athe double", ".athegap", ".atightiger",
        ".atruebody", ".bahjablast", ".big power", ".clamp", ".fit slim", ".full", ".iwanttosmash", ".keep",
        ".livingroom", ".lovehandles", ".mktits", ".musclecurves", ".natural", ".ohmyshoulder", ".prison",
        ".raider", ".run", ".selfworth", ".serious", ".sexyslim", ".smol breast", ".softer", ".steak",
        ".thesaints", ".thespartan", ".thicccccc", ".tight", ".tits", ".tors", ".why", "a2019", "aaaaaaa",
        "aahem", "aatlas", "achristmasevespecial", "adamnson", "anewone", "asoulbody", "atekken", "atest",
        "atooooogit", "avanillacoke", "awonder", "charlotte_body(nude)", "got buns hun", "smooth body",
        "super hero", "yo kinda nice covered"
    };

    // CBBE slider names (from CBBE.xml), without generic words like "Arms" or "Back".
    // Lets presets without any "Breast..." slider (SevenBase, CBBE Vanilla) be recognized.
    const std::unordered_set<std::string> kCBBESliders{
        "7b lower", "7b upper", "applecheeks", "armpitheight", "backarch", "bigbelly", "bigbutt", "bigtorso",
        "buttnew", "buttshape2", "buttsmall", "calfsize", "calfsmooth", "chestdepth", "chestwidth", "chubbyarms",
        "chubbybutt", "chubbylegs", "chubbywaist", "crotchback", "forearmsize", "hipback", "hipbone",
        "hipupperwidth", "kneeheight", "legshape", "legsthin", "musculararms", "muscularbutt", "muscularchest",
        "muscularlegs", "nipbgone", "nippleareola", "nippledistance", "nippledown", "nipplelength", "nippleperk2",
        "nippleperkiness", "nipplesize", "nippletip", "nippleup", "pregnancybelly", "pushup", "roundass",
        "shouldersmooth", "shouldertweak", "shoulderwidth", "slimthighs", "sternumdepth", "sternumheight",
        "tummytuck", "vanillafo4", "waistheight", "widewaistline"
    };

    // Same list as OBody NG (PresetManager.cpp, IsClothedSet): outfit presets.
    constexpr std::array kClothedWords{
        "cloth"sv, "outfit"sv, "nevernude"sv, "bikini"sv, "feet"sv, "hands"sv, "push"sv, "cleavage"sv, "armor"sv
    };

    enum class Body { kBodyTalk, kFusionGirl, kCBBE, kUnknown };

    std::string_view BodyName(Body b) {
        switch (b) {
        case Body::kBodyTalk:   return "BodyTalk";
        case Body::kFusionGirl: return "FusionGirl";
        case Body::kCBBE:       return "CBBE";
        default:                return "unknown";
        }
    }

    std::string Lower(std::string s) {
        std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string Trim(std::string s) {
        const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::ranges::find_if(s, notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
        return s;
    }

    // Characters that are BodyGen syntax: = , | @ / : #
    std::string Clean(std::string s) {
        for (auto& c : s) {
            if (c == '=' || c == ',' || c == '|' || c == '@' || c == '/' || c == ':' || c == '#') {
                c = '_';
            }
        }
        return Trim(std::move(s));
    }

    bool IsZero(const std::string& lower) { return lower.contains("zero"); }

    bool IsClothed(const std::string& lower) {
        return std::ranges::any_of(kClothedWords, [&](std::string_view w) { return lower.contains(w); });
    }

    // Classified by slider names (the set= attribute is not reliable).
    Body Classify(const std::vector<std::string>& names) {
        bool bt = false, fg = false, cb = false;
        for (const auto& n : names) {
            const auto l = Lower(n);
            if (l.starts_with("bt") || l == "balls" || l == "penis length" || l == "penis width" ||
                l == "erection up" || l == "erection down") {
                bt = true;
            }
            if (l.contains("boobs ")) fg = true;
            if (l.starts_with("breast") || kCBBESliders.contains(l)) cb = true;
        }
        // Fusion Girl also has a few "Breast..." sliders (BreastConcave etc.),
        // but CBBE never uses "Boobs ...", so "Boobs " decides.
        if (bt) return Body::kBodyTalk;
        if (fg) return Body::kFusionGirl;
        if (cb) return Body::kCBBE;
        return Body::kUnknown;
    }

    // ---------------------------------------------------------------- ini

    // Reads "key=value" from a simple ini (ignores sections); nullopt if missing.
    std::optional<std::string> ReadIniValue(std::string_view path, std::string_view wantedKey) {
        std::ifstream file{ fs::path{ path } };
        std::string line;
        const auto wanted = Lower(std::string{ wantedKey });
        while (std::getline(file, line)) {
            line = Trim(line);
            if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            if (Lower(Trim(line.substr(0, eq))) == wanted) {
                auto v = Trim(line.substr(eq + 1));
                if (const auto sc = v.find(';'); sc != std::string::npos) v = Trim(v.substr(0, sc));
                return v;
            }
        }
        return std::nullopt;
    }

    // MCM: user changes take priority over the mod defaults.
    std::string Setting(std::string_view key, std::string_view fallback) {
        if (auto v = ReadIniValue(kMcmUserIni, key)) return *v;
        if (auto v = ReadIniValue(kMcmDefaultIni, key)) return *v;
        return std::string{ fallback };
    }

    bool SettingBool(std::string_view key, bool fallback) {
        const auto v = Lower(Setting(key, fallback ? "1" : "0"));
        return v == "1" || v == "true";
    }

    struct Settings {
        int femaleBody;  // 0 Auto, 1 FusionGirl, 2 CBBE
        bool ignoreFG;
        bool ignoreBT;
        bool ignoreCBBE;
        bool ignoreTWB;
        bool filterClothed;
        std::unordered_set<std::string> excludes;  // lowercase, no .xml
    };

    Settings ReadSettings() {
        Settings s{};
        s.femaleBody = std::atoi(Setting("iFemaleBody", "0").c_str());
        s.ignoreFG = SettingBool("bIgnoreFusionGirlPresets", true);
        s.ignoreBT = SettingBool("bIgnoreBodyTalkPresets", false);
        s.ignoreCBBE = SettingBool("bIgnoreCBBEPresets", true);
        s.ignoreTWB = SettingBool("bIgnoreTWBPresets", true);
        s.filterClothed = SettingBool("bFilterOutfitPresets", true);

        std::stringstream ss{ Setting("sExclude", "") };
        std::string item;
        while (std::getline(ss, item, ',')) {
            item = Lower(Trim(item));
            if (item.ends_with(".xml")) item.resize(item.size() - 4);
            if (!item.empty()) s.excludes.insert(item);
        }
        REX::INFO("settings: iFemaleBody={} bIgnoreFusionGirlPresets={} bIgnoreBodyTalkPresets={} bFilterOutfitPresets={} sExclude={} item(s)",
            s.femaleBody, s.ignoreFG, s.ignoreBT, s.filterClothed, s.excludes.size());
        REX::INFO("settings: bIgnoreCBBEPresets={} bIgnoreTWBPresets={}", s.ignoreCBBE, s.ignoreTWB);
        return s;
    }

    // ---------------------------------------------------------------- presets

    struct Template {
        std::string presetName;
        std::string name;   // BodyGen template name
        std::string line;
        std::size_t sliders;
        Body body;
        Preset data;        // kept in memory for the in-game switcher
    };

    // "<Preset>-Refit", "Female-Refit", "Male-Refit": ORefit presets (same naming as
    // OBody NG). Their values are added on top of the body preset while clothed.
    using RefitPresets = std::unordered_map<std::string, std::vector<Slider>>;  // lowercase name -> deltas

    std::vector<Template> LoadPresets(const Settings& cfg, RefitPresets& a_refit) {
        std::vector<Template> all;
        std::error_code ec;
        const fs::path presetDir{ kPresetDir };
        if (!fs::is_directory(presetDir, ec)) {
            REX::ERROR("preset folder not found: {}", fs::absolute(presetDir, ec).string());
            return all;
        }

        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(presetDir, ec)) {
            if (e.is_regular_file(ec) && Lower(e.path().extension().string()) == ".xml") files.push_back(e.path());
        }
        std::ranges::sort(files);

        std::unordered_set<std::string> usedNames;
        for (const auto& file : files) {
            const auto fname = file.filename().string();
            const auto stem = Lower(file.stem().string());
            if (cfg.excludes.contains(stem)) { REX::INFO("'{}': in sExclude, skipped", fname); continue; }
            if (IsZero(stem)) { REX::INFO("'{}': zeroed build preset, always skipped", fname); continue; }
            if (cfg.ignoreFG && kFusionGirlBuiltin.contains(stem)) { REX::INFO("'{}': stock Fusion Girl preset, skipped", fname); continue; }
            if (cfg.ignoreBT && kBodyTalkBuiltin.contains(stem)) { REX::INFO("'{}': stock BodyTalk preset, skipped", fname); continue; }
            if (cfg.ignoreCBBE && kCBBEBuiltin.contains(stem)) { REX::INFO("'{}': stock CBBE presets, skipped", fname); continue; }
            if (cfg.ignoreTWB && kTWBBuiltin.contains(stem)) { REX::INFO("'{}': stock TWB preset, skipped", fname); continue; }
            if (cfg.filterClothed && IsClothed(stem)) { REX::INFO("'{}': outfit preset, skipped", fname); continue; }

            pugi::xml_document doc;
            const auto res = doc.load_file(file.c_str());
            if (!res) {
                REX::WARN("'{}': invalid XML ({}), skipped", fname, res.description());
                continue;
            }
            for (const auto preset : doc.child("SliderPresets").children("Preset")) {
                // per slider: "big" value (/100); falls back to "small" if that is all there is
                std::vector<std::string> order;
                std::vector<double> values;
                for (const auto s : preset.children("SetSlider")) {
                    const std::string n = s.attribute("name").as_string();
                    const double v = s.attribute("value").as_double() / 100.0;
                    const bool big = std::string_view{ s.attribute("size").as_string() } == "big";
                    const auto it = std::ranges::find(order, n);
                    if (it == order.end()) {
                        order.push_back(n);
                        values.push_back(v);
                    } else if (big) {
                        values[static_cast<std::size_t>(it - order.begin())] = v;
                    }
                }

                const std::string presetName = preset.attribute("name").as_string();
                const auto lowerName = Lower(presetName);
                if (cfg.excludes.contains(lowerName)) { REX::INFO("'{}': in sExclude, skipped", presetName); continue; }
                if (lowerName.ends_with("-refit")) {
                    std::vector<Slider> deltas;
                    for (std::size_t i = 0; i < order.size(); ++i) {
                        if (values[i] != 0.0) deltas.push_back({ order[i], static_cast<float>(values[i]) });
                    }
                    REX::INFO("'{}': ORefit preset ({} sliders)", presetName, deltas.size());
                    a_refit[lowerName] = std::move(deltas);
                    continue;
                }
                if (IsZero(lowerName)) { REX::INFO("'{}': zeroed build preset, always skipped", presetName); continue; }
                if (cfg.filterClothed && IsClothed(lowerName)) { REX::INFO("'{}': outfit preset, skipped", presetName); continue; }
                if (order.empty()) { REX::WARN("'{}' ({}): no sliders, skipped", presetName, fname); continue; }

                const auto body = Classify(order);
                if (body == Body::kUnknown) {
                    std::string sample;
                    for (std::size_t i = 0; i < order.size() && i < 6; ++i) sample += (i ? "; " : "") + order[i];
                    REX::WARN("'{}' ({}): unknown body, skipped. Sliders: {}", presetName, fname, sample);
                    continue;
                }

                std::string morphs;
                std::size_t count = 0;
                Preset data;
                data.name = presetName;
                for (std::size_t i = 0; i < order.size(); ++i) {
                    if (values[i] == 0.0) continue;
                    morphs += std::format("{}@{}, ", Clean(order[i]), values[i]);
                    data.sliders.push_back({ order[i], static_cast<float>(values[i]) });
                    ++count;
                }
                if (count == 0) { REX::WARN("'{}': all sliders are zero, skipped", presetName); continue; }

                const auto base = "OBody_" + Clean(presetName);
                auto name = base;
                for (int n = 2; usedNames.contains(Lower(name)); ++n) name = std::format("{}_{}", base, n);
                usedNames.insert(Lower(name));

                // marker last: tells which preset the actor received
                data.marker = std::format("{}{}", kMarkerPrefix, name.substr(6));
                morphs += data.marker + "@1";
                all.push_back({ presetName, name, name + "=" + morphs, count, body, std::move(data) });
            }
        }
        return all;
    }

    // ---------------------------------------------------------------- JSON rules

    // Same keys as OBody NG. Distribution keys become BodyGen lines; ORefit keys
    // go to GetRefitRules() and are merged across ALL files (union), so several
    // outfit packs can each ship their own list. npc-by-name and faction keys
    // are reported in the log and ignored for now.
    struct Rules {
        std::vector<std::string> extraRaces;  // "additionalRaces": treated like HumanRace
        std::vector<std::string> lines;  // extra morphs.ini lines, already in priority order
        std::unordered_set<std::string> blacklistedPresets;  // lowercase
    };

    constexpr std::array kUnsupportedKeys{
        "npc"sv, "factionFemale"sv, "factionMale"sv, "blacklistedNpcs"sv
    };

    // JSON FormID -> BodyGen format: hex without the plugin index (6 digits)
    std::optional<std::string> NormalizeFormID(std::string id) {
        id = Trim(id);
        if (id.starts_with("0x") || id.starts_with("0X")) id = id.substr(2);
        if (id.empty() || id.size() > 8 || !std::ranges::all_of(id, [](unsigned char c) { return std::isxdigit(c); })) {
            return std::nullopt;
        }
        const auto v = std::stoul(id, nullptr, 16) & 0xFFFFFF;
        if (v == 0) return std::nullopt;
        return std::format("{:06X}", v);
    }

    bool IsChildRace(const std::string& race) { return Lower(race).contains("child"); }

    Rules LoadRules(const std::vector<Template>& active) {
        Rules r;
        std::error_code ec;
        const fs::path dir{ kRulesDir };
        if (!fs::is_directory(dir, ec)) {
            REX::INFO("no JSON rules folder ({}), random distribution only", kRulesDir);
            return r;
        }

        std::unordered_map<std::string, std::string> byPreset;  // preset name (lowercase) -> template
        for (const auto& t : active) byPreset.emplace(Lower(t.presetName), t.name);

        // builds "A|B|C" from a list of preset names
        auto templateList = [&](const json& arr, std::string_view where) -> std::string {
            std::string out;
            if (!arr.is_array()) { REX::WARN("{}: expected a list of presets", where); return out; }
            for (const auto& p : arr) {
                if (!p.is_string()) continue;
                const auto it = byPreset.find(Lower(p.get<std::string>()));
                if (it == byPreset.end()) {
                    REX::WARN("{}: preset '{}' is not among the loaded presets, skipped", where, p.get<std::string>());
                    continue;
                }
                out += (out.empty() ? "" : "|") + it->second;
            }
            return out;
        };

        // order matters: in BodyGen the last line matching an NPC wins.
        std::vector<std::string> raceLines, pluginLines, npcLines, blacklistLines;

        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.is_regular_file(ec) && Lower(e.path().extension().string()) == ".json") files.push_back(e.path());
        }
        std::ranges::sort(files);

        for (const auto& file : files) {
            const auto fname = file.filename().string();
            json j;
            try {
                std::ifstream in{ file };
                j = json::parse(in, nullptr, true, true);  // allows comments
            } catch (const std::exception& e) {
                REX::ERROR("'{}': invalid JSON ({}), file skipped", fname, e.what());
                continue;
            }
            if (!j.is_object()) continue;
            REX::INFO("rules: reading '{}'", fname);

            for (const auto key : kUnsupportedKeys) {
                if (j.contains(key) && !j[key].empty()) {
                    REX::WARN("'{}': key '{}' is not supported in FO4 yet, ignored", fname, key);
                }
            }

            // Custom adult races that use the human body (e.g. a standalone race mod):
            // they get the same random distribution, the menu and ORefit.
            if (j.contains("additionalRaces") && j["additionalRaces"].is_array()) {
                for (const auto& race : j["additionalRaces"]) {
                    if (!race.is_string() || race.get<std::string>().empty()) continue;
                    const auto name = race.get<std::string>();
                    if (IsChildRace(name)) { REX::WARN("'{}': race '{}' is a child race, never gets a preset", fname, name); continue; }
                    if (GetAllowedRaces().insert(Lower(name)).second) r.extraRaces.push_back(name);
                }
            }

            for (const auto& [gender, key] : { std::pair{ "Female"sv, "raceFemale"sv }, std::pair{ "Male"sv, "raceMale"sv } }) {
                if (!j.contains(key) || !j[key].is_object()) continue;
                for (const auto& [race, presets] : j[key].items()) {
                    if (IsChildRace(race)) { REX::WARN("'{}': race '{}' is a child race, never gets a preset", fname, race); continue; }
                    const auto list = templateList(presets, std::format("{} {} {}", fname, key, race));
                    if (!list.empty()) raceLines.push_back(std::format("All|{}|{}={}", gender, race, list));
                }
            }

            // BodyGen needs a race on plugin lines, otherwise they match nobody
            for (const auto& [gender, key] : { std::pair{ "Female"sv, "npcPluginFemale"sv }, std::pair{ "Male"sv, "npcPluginMale"sv } }) {
                if (!j.contains(key) || !j[key].is_object()) continue;
                for (const auto& [plugin, presets] : j[key].items()) {
                    const auto list = templateList(presets, std::format("{} {} {}", fname, key, plugin));
                    if (!list.empty()) pluginLines.push_back(std::format("{}|All|{}|%RACE%={}", plugin, gender, list));
                }
            }

            if (j.contains("npcFormID") && j["npcFormID"].is_object()) {
                for (const auto& [plugin, ids] : j["npcFormID"].items()) {
                    if (!ids.is_object()) continue;
                    for (const auto& [id, presets] : ids.items()) {
                        const auto fid = NormalizeFormID(id);
                        if (!fid) { REX::WARN("'{}': invalid FormID '{}' in {}", fname, id, plugin); continue; }
                        const auto list = templateList(presets, std::format("{} npcFormID {}|{}", fname, plugin, id));
                        if (!list.empty()) npcLines.push_back(std::format("{}|{}={}", plugin, *fid, list));
                    }
                }
            }

            if (j.contains("blacklistedNpcsFormID") && j["blacklistedNpcsFormID"].is_object()) {
                for (const auto& [plugin, ids] : j["blacklistedNpcsFormID"].items()) {
                    if (!ids.is_array()) continue;
                    for (const auto& id : ids) {
                        if (!id.is_string()) continue;
                        if (const auto fid = NormalizeFormID(id.get<std::string>())) {
                            blacklistLines.push_back(std::format("{}|{}={}", plugin, *fid, kBlacklistTemplate));
                        } else {
                            REX::WARN("'{}': invalid FormID '{}' in blacklistedNpcsFormID", fname, id.get<std::string>());
                        }
                    }
                }
            }

            for (const auto& [gender, key] : { std::pair{ "Female"sv, "blacklistedNpcsPluginFemale"sv }, std::pair{ "Male"sv, "blacklistedNpcsPluginMale"sv } }) {
                if (!j.contains(key) || !j[key].is_array()) continue;
                for (const auto& plugin : j[key]) {
                    if (plugin.is_string()) blacklistLines.push_back(std::format("{}|All|{}|%RACE%={}", plugin.get<std::string>(), gender, kBlacklistTemplate));
                }
            }

            for (const auto& [gender, key] : { std::pair{ "Female"sv, "blacklistedRacesFemale"sv }, std::pair{ "Male"sv, "blacklistedRacesMale"sv } }) {
                if (!j.contains(key) || !j[key].is_array()) continue;
                for (const auto& race : j[key]) {
                    if (race.is_string()) blacklistLines.push_back(std::format("All|{}|{}={}", gender, race.get<std::string>(), kBlacklistTemplate));
                }
            }

            // ---- ORefit lists: union of every file
            auto& refit = GetRefitRules();
            auto readFormIDs = [&](std::string_view a_key, auto& a_out) {
                if (!j.contains(a_key) || !j[a_key].is_object()) return;
                for (const auto& [plugin, ids] : j[a_key].items()) {
                    if (!ids.is_array()) continue;
                    for (const auto& id : ids) {
                        const auto fid = id.is_string() ? NormalizeFormID(id.get<std::string>()) : std::nullopt;
                        if (fid) {
                            a_out.emplace_back(plugin, static_cast<std::uint32_t>(std::stoul(*fid, nullptr, 16)));
                        } else {
                            REX::WARN("'{}': invalid FormID in {} ({})", fname, a_key, plugin);
                        }
                    }
                }
            };
            auto readNames = [&](std::string_view a_key, std::unordered_set<std::string>& a_out) {
                if (!j.contains(a_key) || !j[a_key].is_array()) return;
                for (const auto& n : j[a_key]) {
                    if (n.is_string()) a_out.insert(Lower(n.get<std::string>()));
                }
            };
            readFormIDs("blacklistedOutfitsFromORefitFormID", refit.blacklistFormIDs);
            readFormIDs("outfitsForceRefitFormID", refit.forceFormIDs);
            readNames("blacklistedOutfitsFromORefit", refit.blacklistNames);
            readNames("blacklistedOutfitsFromORefitPlugin", refit.blacklistPlugins);
            readNames("outfitsForceRefit", refit.forceNames);
            if (j.contains("orefitBodySlots") && j["orefitBodySlots"].is_array()) {
                for (const auto& slot : j["orefitBodySlots"]) {
                    if (slot.is_number_integer()) refit.extraSlots.insert(slot.get<int>());
                }
            }

            if (j.contains("blacklistedPresetsFromRandomDistribution") && j["blacklistedPresetsFromRandomDistribution"].is_array()) {
                for (const auto& p : j["blacklistedPresetsFromRandomDistribution"]) {
                    if (p.is_string()) r.blacklistedPresets.insert(Lower(p.get<std::string>()));
                }
            }
        }

        // plugin lines need a race: one line per allowed race
        auto expand = [&](std::vector<std::string>& a_lines) {
            std::vector<std::string> out;
            for (const auto& l : a_lines) {
                const auto pos = l.find("%RACE%");
                if (pos == std::string::npos) { out.push_back(l); continue; }
                out.push_back(std::string{ l }.replace(pos, 6, "HumanRace"));
                for (const auto& race : r.extraRaces) out.push_back(std::string{ l }.replace(pos, 6, race));
            }
            a_lines = std::move(out);
        };
        expand(pluginLines);
        expand(blacklistLines);

        for (auto* v : { &raceLines, &pluginLines, &npcLines, &blacklistLines }) {
            r.lines.insert(r.lines.end(), v->begin(), v->end());
        }
        REX::INFO("rules: {} by race, {} by plugin, {} by NPC, {} blacklist, {} presets excluded from random distribution",
            raceLines.size(), pluginLines.size(), npcLines.size(), blacklistLines.size(), r.blacklistedPresets.size());
        return r;
    }

    // ---------------------------------------------------------------- output

    std::string ReadWhole(const fs::path& p) {
        std::ifstream f{ p, std::ios::binary };
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    // Only writes when the content changed: no pointless writes (or MO2 overwrite churn).
    void WriteIfChanged(const fs::path& p, const std::string& content) {
        std::error_code ec;
        if (fs::exists(p, ec) && ReadWhole(p) == content) {
            REX::INFO("{} unchanged", p.filename().string());
            return;
        }
        std::ofstream f{ p, std::ios::binary | std::ios::trunc };  // no BOM
        f << content;
        if (!f) {
            REX::ERROR("failed to write {}", p.string());
            return;
        }
        REX::INFO("{} written", p.filename().string());
    }

    // ---------------------------------------------------------------- ORefit sliders

    // Built-in Fusion Girl clothed adjustment, used when there is no Refit preset.
    // Same intent as OBody NG's GenerateClotheSliders (push-up, breasts together and
    // higher, flatter nipples), mapped to Fusion Girl slider names. Starting values:
    // tune them with a "Female-Refit" preset instead of editing code.
    struct DefaultRefit {
        std::string_view name;
        float value;
        bool absolute;  // true = set to value, false = add value
    };
    constexpr std::array kFusionGirlRefit{
        DefaultRefit{ "Boobs Together"sv, 0.30f, false },
        DefaultRefit{ "Boobs Cleavage"sv, 0.30f, false },
        DefaultRefit{ "Boobs Height"sv, 0.15f, false },
        DefaultRefit{ "Boobs Gravity"sv, -0.10f, false },
        DefaultRefit{ "Boobs Big"sv, -0.05f, false },
        DefaultRefit{ "Bum Apple"sv, -0.05f, false },
        DefaultRefit{ "Nipples Puffy"sv, 0.0f, true },
        DefaultRefit{ "Nipples Length"sv, -0.30f, false },
    };

    // Deltas for one preset: "<Preset>-Refit", else "Female-Refit"/"Male-Refit", else
    // the built-in Fusion Girl values (women) or nothing (men). a_presetName empty =
    // generic (for actors without one of our presets).
    // CBBE version of OBody NG's GenerateClotheSliders, with the FO4 CBBE slider names
    // that exist (sliders FO4 CBBE does not have are left out).
    constexpr std::array kCBBERefit{
        DefaultRefit{ "BreastsCleavage"sv, 1.0f, true },
        DefaultRefit{ "BreastGravity2"sv, -0.1f, false },
        DefaultRefit{ "BreastTopSlope"sv, -0.2f, false },
        DefaultRefit{ "BreastsTogether"sv, 0.3f, false },
        DefaultRefit{ "Breasts"sv, -0.05f, false },
        DefaultRefit{ "BreastHeight"sv, 0.15f, false },
        DefaultRefit{ "AppleCheeks"sv, -0.05f, false },
        DefaultRefit{ "Butt"sv, -0.05f, false },
        DefaultRefit{ "NippleTip"sv, 0.0f, true },
        DefaultRefit{ "NipBGone"sv, 1.0f, true },
        DefaultRefit{ "NippleDistance"sv, 0.05f, false },
    };

    Body activeFemaleBody = Body::kFusionGirl;

    std::vector<RefitDelta> BuildRefit(std::string_view a_presetName, bool a_female, const RefitPresets& a_refit) {
        std::vector<RefitDelta> out;
        auto own = a_presetName.empty() ? a_refit.end() : a_refit.find(Lower(std::string{ a_presetName }) + "-refit");
        auto generic = a_refit.find(a_female ? "female-refit" : "male-refit");
        if (own != a_refit.end()) {
            for (const auto& s : own->second) out.push_back({ s.name, s.value, false });
        } else if (generic != a_refit.end()) {
            for (const auto& s : generic->second) out.push_back({ s.name, s.value, false });
        } else if (a_female && activeFemaleBody == Body::kCBBE) {
            for (const auto& d : kCBBERefit) out.push_back({ std::string{ d.name }, d.value, d.absolute });
        } else if (a_female) {
            for (const auto& d : kFusionGirlRefit) out.push_back({ std::string{ d.name }, d.value, d.absolute });
        }
        return out;
    }

    void Generate() {
        const auto cfg = ReadSettings();
        RefitPresets refitPresets;
        auto all = LoadPresets(cfg, refitPresets);

        std::size_t fg = 0, cb = 0;
        for (const auto& t : all) {
            if (t.body == Body::kFusionGirl) ++fg;
            if (t.body == Body::kCBBE) ++cb;
        }

        Body female = Body::kFusionGirl;
        if (cfg.femaleBody == 2) {
            female = Body::kCBBE;
        } else if (cfg.femaleBody != 1) {  // Auto
            if (cb > 0 && fg == 0) female = Body::kCBBE;
            if (fg > 0 && cb > 0) {
                REX::WARN("found FusionGirl ({}) and CBBE ({}) presets; using FusionGirl (change it in the MCM if you use CBBE)", fg, cb);
            }
        }
        REX::INFO("female body: {}", BodyName(female));
        activeFemaleBody = female;

        GetGenericRefit(false) = BuildRefit({}, false, refitPresets);
        GetGenericRefit(true) = BuildRefit({}, true, refitPresets);

        std::vector<Template> active;
        GetPresets(false).clear();
        GetPresets(true).clear();
        for (auto& t : all) {
            t.data.refitDeltas = BuildRefit(t.data.name, t.body != Body::kBodyTalk, refitPresets);
            if (t.body == Body::kBodyTalk || t.body == female) {
                active.push_back(t);
                GetPresets(t.body != Body::kBodyTalk).push_back(t.data);
            } else {
                REX::INFO("'{}' is {}, skipped (active female body is {})", t.presetName, BodyName(t.body), BodyName(female));
            }
        }

        const auto rules = LoadRules(active);

        std::string templates = "# Generated by OBodyFO4 on every game start. Do not edit.\n";
        templates += std::format("{}={}None@0\n", kBlacklistTemplate, kMarkerPrefix);

        std::string femaleList, maleList;
        std::size_t nf = 0, nm = 0;
        for (const auto& t : active) {
            templates += t.line + "\n";
            const bool random = !rules.blacklistedPresets.contains(Lower(t.presetName));
            REX::INFO("{} -> {} ({} sliders){}", t.presetName, BodyName(t.body), t.sliders, random ? "" : " [not in random distribution]");
            if (!random) continue;
            if (t.body == Body::kBodyTalk) {
                maleList += (nm++ ? "|" : "") + t.name;
            } else {
                femaleList += (nf++ ? "|" : "") + t.name;
            }
        }

        // HumanRace does not include children (HumanChildRace): children never get a preset.
        std::string morphs = "# Generated by OBodyFO4 on every game start. Do not edit.\n";
        if (nf) morphs += "All|Female|HumanRace=" + femaleList + "\n";
        if (nm) morphs += "All|Male|HumanRace=" + maleList + "\n";
        for (const auto& race : rules.extraRaces) {
            if (nf) morphs += "All|Female|" + race + "=" + femaleList + "\n";
            if (nm) morphs += "All|Male|" + race + "=" + maleList + "\n";
        }
        if (!rules.extraRaces.empty()) REX::INFO("additional races treated as human: {}", rules.extraRaces.size());
        for (const auto& l : rules.lines) morphs += l + "\n";
        if (!nf) REX::WARN("no female presets in random distribution: women will not be randomized");
        if (!nm) REX::WARN("no male presets in random distribution: men will not be randomized");

        std::error_code ec;
        const fs::path outDir{ kOutDir };
        fs::create_directories(outDir, ec);
        WriteIfChanged(outDir / kTemplatesFile, templates);
        WriteIfChanged(outDir / kMorphsFile, morphs);

        REX::INFO("{} female and {} male presets in random distribution", nf, nm);
    }

    void CheckLooksMenuBodyGen() {
        // f4ee_custom.ini takes priority over f4ee.ini
        auto v = ReadIniValue(kLooksMenuCustomIni, "bEnableBodyGen");
        if (!v) v = ReadIniValue(kLooksMenuIni, "bEnableBodyGen");
        if (v && *v == "0") {
            REX::ERROR("f4ee.ini has bEnableBodyGen=0: LooksMenu will not distribute anything. Set it to 1.");
        } else {
            REX::INFO("f4ee.ini bEnableBodyGen={}", v ? *v : "(not found, LooksMenu default = 1)");
        }
    }

}  // namespace

// Only used by the old F4SE for 1.10.163 (OG). NG/AE F4SE reads F4SEPlugin_Version instead.
extern "C" __declspec(dllexport) bool F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
    a_info->infoVersion = F4SE::PluginInfo::kVersion;
    a_info->name = "OBodyFO4";
    a_info->version = 91;
    return !a_f4se->IsEditor();
}

void OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
{
    switch (a_msg->type) {
    case F4SE::MessagingInterface::kGameDataReady:
        SpeedUpBodyGen();
        HookCrosshair();
        InstallRefit();
        break;
    case F4SE::MessagingInterface::kPostLoadGame:
    case F4SE::MessagingInterface::kNewGame:
        HookCrosshair();  // the source can be created late; LL FourPlay hooks here too
        break;
    default:
        break;
    }
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
    F4SE::Init(a_f4se);
    REX::INFO("OBodyFO4 loaded");
    CheckLooksMenuBodyGen();
    Generate();
    RegisterPapyrus();
    if (const auto messaging = F4SE::GetMessagingInterface()) {
        messaging->RegisterListener(OnF4SEMessage);
    }
    return true;
}
