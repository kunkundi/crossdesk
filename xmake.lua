set_project("crossdesk")
set_license("LGPL-3.0")

includes("xmake/options.lua")
includes("xmake/platform.lua")
includes("xmake/targets.lua")

setup_options_and_dependencies()
setup_platform_settings()
setup_targets()
