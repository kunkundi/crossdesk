function setup_options_and_dependencies()
    option("CROSSDESK_VERSION")
        set_default("0.0.0")
        set_showmenu(true)
        set_description("Set CROSSDESK_VERSION for build")
    option_end()

    option("USE_CUDA")
        set_default(false)
        set_showmenu(true)
        set_description("Use CUDA for hardware codec acceleration")
    option_end()

    option("USE_WAYLAND")
        set_default(false)
        set_showmenu(true)
        set_description("Enable Wayland capture on Linux (assumes dependencies are installed)")
    option_end()

    option("USE_DRM")
        set_default(false)
        set_showmenu(true)
        set_description("Enable DRM capture on Linux (assumes dependencies are installed)")
    option_end()

    add_rules("mode.release", "mode.debug")
    set_languages("c++17")
    set_encodings("utf-8")

    -- set_policy("build.warning", true)
    -- set_warnings("all", "extra")
    -- add_cxxflags("/W4", "/WX")

    add_defines("UNICODE")
    add_defines("USE_CUDA=" .. (is_config("USE_CUDA", true) and "1" or "0"))
    add_defines("USE_WAYLAND=" .. (is_config("USE_WAYLAND", true) and "1" or "0"))
    add_defines("USE_DRM=" .. (is_config("USE_DRM", true) and "1" or "0"))

    if is_mode("debug") then
        add_defines("CROSSDESK_DEBUG")
    end

    add_requireconfs("*.python", {version = "3.12", override = true, configs = {pgo = false}})
    add_requires("spdlog 1.14.1", {system = false})
    add_requires("imgui v1.92.1-docking", {configs = {sdl3 = true, sdl3_renderer = true}})
    add_requires("openssl3 3.3.2", {system = false})
    add_requires("nlohmann_json 3.11.3")
    add_requires("cpp-httplib v0.26.0", {configs = {ssl = true}})
    add_requires("tinyfiledialogs 3.15.1")
end