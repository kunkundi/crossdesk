set_project("crossdesk")
set_license("GPL-3.0-only")

local source_dir = os.getenv("CROSSDESK_SOURCE_DIR") or os.scriptdir()
local desktop_platform = is_plat("windows", "macosx", "linux")

add_rules("mode.release", "mode.debug")
set_languages("c++17")
set_encodings("utf-8")
add_requires("nlohmann_json 3.11.3")

if desktop_platform then
    includes(path.join(source_dir, "apps/desktop/xmake/options.lua"))
    includes(path.join(source_dir, "apps/desktop/xmake/platform.lua"))
    includes(path.join(source_dir, "apps/desktop/xmake/rules/slint.lua"))
    includes(path.join(source_dir, "apps/desktop/xmake/targets.lua"))

    setup_options_and_dependencies()
    setup_platform_settings()
end

local wire_dir = path.join(source_dir, "libs/wire")

target("crossdesk_wire")
    set_kind("static")
    add_packages("nlohmann_json")
    add_files(path.join(wire_dir, "src/*.cpp"))
    add_headerfiles(path.join(wire_dir,
        "include/(*.h)"))
    add_includedirs(path.join(wire_dir, "include"), {public = true})
target_end()

target("crossdesk_wire_test")
    set_kind("binary")
    set_default(false)
    add_deps("crossdesk_wire")
    add_files(path.join(wire_dir, "tests/wire_test.cpp"))
target_end()

target("keyboard_state_wire_test")
    set_kind("binary")
    set_default(false)
    add_deps("crossdesk_wire")
    add_files(path.join(wire_dir,
        "tests/keyboard_state_wire_test.cpp"))
target_end()

if desktop_platform then
    setup_targets()
end
