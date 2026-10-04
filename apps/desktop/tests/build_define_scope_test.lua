-- Run from the repository root: xmake lua apps/desktop/tests/build_define_scope_test.lua windows true
-- Inspect the real Xmake target graph without configuring/downloading packages.
function main(plat, portable)
    import("core.project.config")
    import("core.project.project")
    config.load()
    config.set("plat", plat or "windows", {force = true})
    config.set("arch", plat == "macosx" and "arm64" or "x64", {force = true})
    config.set("CROSSDESK_PORTABLE", portable == "true", {force = true})
    config.set("CROSSDESK_VERSION", "v1.5.3-2-20260913", {force = true})
    local targets = project.targets()
    local function flatten(values)
        local result = {}
        local function append(value)
            if type(value) == "table" then
                for _, entry in ipairs(value) do append(entry) end
            elseif value ~= nil then
                table.insert(result, value)
            end
        end
        append(values)
        return result
    end
    local function contains(values, expected)
        return table.contains(flatten(values), expected)
    end
    local function effective_defines(target)
        return table.join(table.wrap(target:get("defines")),
            flatten(target:get_from("defines", "dep::*")))
    end

    for _, name in ipairs({"minirtc", "media", "common", "crossdesk_wire", "rd_log"}) do
        for _, define in ipairs(effective_defines(targets[name])) do
            assert(not define:startswith("CROSSDESK_VERSION"), name .. " inherits version macros")
            assert(not define:startswith("CROSSDESK_PORTABLE"), name .. " inherits portable macros")
        end
    end
    for _, name in ipairs({"gui", "crossdesk", "path_manager"}) do
        assert(contains(effective_defines(targets[name]), "CROSSDESK_PORTABLE=1") ==
            (portable == "true"), name .. " has inconsistent portable layout/configuration")
    end

    local gui = targets.gui
    local version_files = {
        ["apps/desktop/src/gui/application/gui_application.cpp"] = true,
        ["apps/desktop/src/gui/runtime/peer_event_handler.cpp"] = true
    }
    local seen = {}
    for _, file in ipairs(gui:sourcefiles()) do
        assert(not seen[file], "duplicate GUI source: " .. file)
        seen[file] = true
        local defines = (gui:fileconfig(file) or {}).defines
        assert(contains(defines, 'CROSSDESK_VERSION="v1.5.3-2-20260913"') ==
            (version_files[file] == true), "unexpected version scope: " .. file)
    end
    for file in pairs(version_files) do
        assert(seen[file], "missing version consumer: " .. file)
    end

    if plat == "windows" then
        for _, name in ipairs({"autostart", "windows_autostart_test"}) do
            assert(contains(effective_defines(targets[name]), "CROSSDESK_PORTABLE=1") ==
                (portable == "true"), name .. " has inconsistent autostart privilege policy")
        end
        local resources = {
            crossdesk = portable == "true" and "crossdesk_portable" or "crossdesk",
            crossdesk_service = "crossdesk_service",
            crossdesk_session_helper = "crossdesk_session_helper",
            crossdesk_privacy_window = "crossdesk_privacy_window",
            wgc_plugin = "wgc_plugin"
        }
        for name, resource in pairs(resources) do
            local defines = targets[name]:fileconfig(
                "apps/desktop/resources/windows/" .. resource .. ".rc").defines
            assert(contains(defines, 'CROSSDESK_VERSION_STRING="v1.5.3-2-20260913"'))
            assert(contains(defines, "CROSSDESK_VERSION_NUMERIC=1,5,3,0"))
            assert(contains(defines, "CROSSDESK_PORTABLE=1") ==
                (name == "crossdesk_session_helper" and portable == "true"))
        end
    end
    print("Build define scopes passed: %s portable=%s", plat, portable)
end
