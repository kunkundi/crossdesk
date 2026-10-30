set_project("crossdesk")
set_xmakever("3.1.1")
set_license("GPL-3.0-only")

local source_dir = os.getenv("CROSSDESK_SOURCE_DIR") or os.scriptdir()
local desktop_platform = is_plat("windows", "macosx", "linux") or
    (not is_plat("iphoneos", "android", "harmony") and is_host("windows", "macosx", "linux"))

add_rules("mode.release", "mode.debug")
set_languages("c++17")
set_encodings("utf-8")
add_requires("nlohmann_json 3.11.3")

option("CROSSDESK_BUILD_DESKTOP")
    set_default(desktop_platform)
    set_showmenu(true)
    set_description("Build the desktop client")
option_end()

option("CROSSDESK_BUILD_CLI")
    set_default(false)
    set_showmenu(true)
    set_description("Build the terminal controller")
option_end()

local build_desktop = desktop_platform and not is_config("CROSSDESK_BUILD_DESKTOP", false)
local build_cli = has_config("CROSSDESK_BUILD_CLI")
if build_cli and not desktop_platform then
    raise("CrossDesk CLI supports Windows, macOS and Linux")
end

if desktop_platform then
    includes(path.join(source_dir, "apps/desktop/xmake/options.lua"))
    setup_options_and_dependencies(build_desktop)
end

if build_desktop then
    includes(path.join(source_dir, "apps/desktop/xmake/platform.lua"))
    includes(path.join(source_dir, "apps/desktop/xmake/rules/slint.lua"))
    includes(path.join(source_dir, "apps/desktop/xmake/rules/about_resources.lua"))
    includes(path.join(source_dir, "apps/desktop/xmake/targets.lua"))

    setup_platform_settings()
end

local wire_dir = path.join(source_dir, "libs/wire")

target("crossdesk_wire")
    set_kind("static")
    add_packages("nlohmann_json", {public = true})
    add_files(path.join(wire_dir, "src/*.cpp"))
    add_headerfiles(path.join(wire_dir,
        "include/(*.h)"))
    add_includedirs(path.join(wire_dir, "include"), {public = true})
target_end()

-- CLI source packages omit tests; full checkouts retain these existing targets.
if os.isdir(path.join(wire_dir, "tests")) then
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
end

if build_desktop then
    setup_targets()
end

if build_cli then
    -- Desktop setup already includes MiniRTC and the shared OpenSSL recipe.
    if not build_desktop then
        add_requireconfs("**.openssl", "**.openssl3",
            {version = "3.3.2", system = false, override = true})
        includes(path.join(source_dir, "deps/thirdparty/openssl"))
        includes(path.join(source_dir, "deps/submodules/minirtc"))
    end
    includes(path.join(source_dir, "apps/cli/xmake.lua"))
end
