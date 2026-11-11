set_project("OBodyFO4")
set_version("0.9.1")
set_languages("c++23")
set_arch("x64")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

add_requires("pugixml", "nlohmann_json")

-- commonlibf4 sits next to this folder: D:\FO4Xmake\commonlibf4
includes("../commonlibf4")

target("OBodyFO4")
    set_kind("shared")
    add_rules("commonlibf4.plugin", {
        name = "OBodyFO4",
        author = "Charolas",
    })
    add_files("src/*.cpp")
    add_packages("pugixml", "nlohmann_json")
    set_pcxxheader("src/PCH.h")
    -- shipped with xmake install: MCM, example rules and the compiled script
    add_installfiles("dist/(MCM/**)")
    add_installfiles("dist/(F4SE/Plugins/OBodyFO4/**)")
    add_installfiles("dist/(Scripts/**)")
    add_installfiles("dist/(Interface/**)")
