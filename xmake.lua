set_project("crossdesk")
set_license("GPL-3.0-only")

add_repositories("crossdesk-packages " .. path.join(os.scriptdir(), "xmake", "repository"))

includes("xmake/options.lua")
includes("xmake/platform.lua")
includes("xmake/rules/slint.lua")
includes("xmake/targets.lua")

setup_options_and_dependencies()
setup_platform_settings()
setup_targets()
