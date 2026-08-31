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

    option("CROSSDESK_PORTABLE")
        set_default(false)
        set_showmenu(true)
        set_description("Build CrossDesk as a portable package that stores data beside the executable")
    option_end()

    -- Preserve the existing codebase/toolchain contract. Slint targets opt in to
    -- C++20 locally because its generated C++ API requires it.
    local crossdesk_version = get_config("CROSSDESK_VERSION") or "0.0.0"
    local version_base = crossdesk_version:gsub("^v", ""):match("^(%d[%d%.]*)") or "0.0.0"
    local version_parts = {}
    for part in version_base:gmatch("%d+") do
        local value = tonumber(part) or 0
        if value > 65535 then
            value = 0
        end
        table.insert(version_parts, value)
        if #version_parts == 4 then
            break
        end
    end
    while #version_parts < 4 do
        table.insert(version_parts, 0)
    end

    add_defines("CROSSDESK_VERSION_STRING=\"" .. crossdesk_version .. "\"")
    if is_os("windows") then
        add_defines("CROSSDESK_VERSION_NUMERIC=" .. table.concat(version_parts, ","))
    end

    -- set_policy("build.warning", true)
    -- set_warnings("all", "extra")
    -- add_cxxflags("/W4", "/WX")

    add_defines("UNICODE")
    add_defines("USE_CUDA=" .. (is_config("USE_CUDA", true) and "1" or "0"))
    add_defines("USE_WAYLAND=" .. (is_config("USE_WAYLAND", true) and "1" or "0"))
    add_defines("USE_DRM=" .. (is_config("USE_DRM", true) and "1" or "0"))
    if is_config("CROSSDESK_PORTABLE", true) then
        add_defines("CROSSDESK_PORTABLE=1")
    end

    if is_mode("debug") then
        add_defines("CROSSDESK_DEBUG")
    end

    add_requireconfs("**.python", {version = "3.12", override = true, configs = {pgo = false}})
    add_requires("spdlog 1.14.1", {system = false})
    add_requires("slint 1.17.1", {configs = {shared = true}})
    add_requires("libsdl3 3.2.26", {configs = {shared = false}})
    add_requires("openssl3 3.3.2", {system = false})
    add_requires("cpp-httplib v0.26.0", {configs = {ssl = true}})
    add_requires("tinyfiledialogs 3.15.1")
end
