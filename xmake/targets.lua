function setup_targets()
    add_packages("spdlog", "imgui", "nlohmann_json")

    includes("submodules", "thirdparty")

    local crossdesk_windows_resource = "scripts/windows/crossdesk.rc"
    if is_config("CROSSDESK_PORTABLE", true) then
        crossdesk_windows_resource = "scripts/windows/crossdesk_portable.rc"
    end

    target("rd_log")
        set_kind("object")
        add_packages("spdlog")
        add_files("src/log/rd_log.cpp")
        add_includedirs("src/log", {public = true})

    target("common")
        set_kind("object")
        add_deps("rd_log")
        add_files("src/common/*.cpp")
        if is_os("macosx") then
            add_files("src/common/*.mm")
        end
        add_includedirs("src/common", {public = true})

    target("path_manager")
        set_kind("object")
        add_deps("rd_log")
        add_includedirs("src/path_manager", {public = true})
        add_files("src/path_manager/*.cpp")
        add_includedirs("src/path_manager", {public = true})

    target("path_manager_portable_test")
        set_kind("binary")
        set_default(false)
        add_defines("CROSSDESK_PORTABLE=1")
        add_includedirs("src/path_manager")
        add_files("tests/path_manager_portable_test.cpp",
            "src/path_manager/path_manager.cpp")

    target("macos_keyboard_modifier_state_test")
        set_kind("binary")
        set_default(false)
        add_includedirs("src/device_controller")
        add_files("tests/macos_keyboard_modifier_state_test.cpp")

    target("keyboard_state_protocol_test")
        set_kind("binary")
        set_default(false)
        add_includedirs("src/device_controller", "src/common")
        add_files("tests/keyboard_state_protocol_test.cpp")

    target("connection_status_protocol_test")
        set_kind("binary")
        set_default(false)
        add_files("tests/connection_status_protocol_test.cpp")

    target("windows_manifest_resource_test")
        set_kind("binary")
        set_default(false)
        add_files("tests/windows_manifest_resource_test.cpp")

    target("windows_service_mouse_ipc_test")
        set_kind("binary")
        set_default(false)
        add_files("tests/windows_service_mouse_ipc_test.cpp")

    target("windows_mouse_controller_safety_test")
        set_kind("binary")
        set_default(false)
        add_files("tests/windows_mouse_controller_safety_test.cpp")

    target("windows_sas_guard_test")
        set_kind("binary")
        set_default(false)
        add_includedirs("src/service/windows")
        add_files("tests/windows_sas_guard_test.cpp")

    target("display_popup_hover_state_test")
        set_kind("binary")
        set_default(false)
        add_files("tests/display_popup_hover_state_test.cpp")

    target("version_checker_test")
        set_kind("binary")
        set_default(false)
        add_packages("cpp-httplib")
        add_deps("rd_log")
        add_includedirs("src/version_checker")
        add_files("tests/version_checker_test.cpp",
            "src/version_checker/version_checker.cpp")
        if is_os("macosx") then
            add_defines("CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN")
            add_frameworks("Security", "CoreFoundation")
        end

    target("screen_capturer")
        set_kind("object")
        add_deps("rd_log", "common")
        add_includedirs("src/screen_capturer", {public = true})
        if is_os("windows") then
            add_packages("libyuv")
            add_files("src/screen_capturer/windows/screen_capturer_dxgi.cpp",
                "src/screen_capturer/windows/screen_capturer_gdi.cpp",
                "src/screen_capturer/windows/screen_capturer_win.cpp")
            add_includedirs("src/screen_capturer/windows", "src/service/windows",
                {public = true})
        elseif is_os("macosx") then
            add_files("src/screen_capturer/macosx/*.cpp",
                "src/screen_capturer/macosx/*.mm")
            add_includedirs("src/screen_capturer/macosx", {public = true})
        elseif is_os("linux") then
            add_packages("libyuv")
            add_files("src/screen_capturer/linux/screen_capturer_linux.cpp")
            add_files("src/screen_capturer/linux/screen_capturer_x11.cpp")
            add_files("src/screen_capturer/linux/screen_capturer_drm.cpp")
            if is_config("USE_WAYLAND", true) then
                add_files("src/screen_capturer/linux/screen_capturer_wayland.cpp")
                add_files("src/screen_capturer/linux/screen_capturer_wayland_portal.cpp")
                add_files("src/screen_capturer/linux/screen_capturer_wayland_pipewire.cpp")
            end
            add_includedirs("src/screen_capturer/linux", {public = true})
        end

    target("speaker_capturer")
        set_kind("object")
        add_deps("rd_log")
        add_includedirs("src/speaker_capturer", {public = true})
        if is_os("windows") then
            add_packages("miniaudio")
            add_files("src/speaker_capturer/windows/*.cpp")
            add_includedirs("src/speaker_capturer/windows", {public = true})
        elseif is_os("macosx") then
            add_files("src/speaker_capturer/macosx/*.cpp",
                "src/speaker_capturer/macosx/*.mm")
            add_includedirs("src/speaker_capturer/macosx", {public = true})
        elseif is_os("linux") then
            add_files("src/speaker_capturer/linux/*.cpp")
            add_includedirs("src/speaker_capturer/linux", {public = true})
        end

    target("device_controller")
        set_kind("object")
        add_deps("rd_log", "common")
        add_includedirs("src/device_controller", {public = true})
        if is_os("windows") then
            add_files("src/device_controller/mouse/windows/*.cpp",
                "src/device_controller/keyboard/windows/*.cpp")
            add_includedirs("src/device_controller/mouse/windows",
                "src/device_controller/keyboard/windows", {public = true})
        elseif is_os("macosx") then
            add_files("src/device_controller/mouse/mac/*.cpp",
                "src/device_controller/keyboard/mac/*.cpp")
            add_includedirs("src/device_controller/mouse/mac",
                "src/device_controller/keyboard/mac", {public = true})
        elseif is_os("linux") then
            add_files("src/device_controller/mouse/linux/*.cpp",
                "src/device_controller/keyboard/linux/*.cpp")
            add_includedirs("src/device_controller/mouse/linux",
                "src/device_controller/keyboard/linux", {public = true})
        end

    target("thumbnail")
        set_kind("object")
        add_packages("libyuv", "openssl3")
        add_deps("rd_log", "common")
        add_files("src/thumbnail/*.cpp")
        add_includedirs("src/thumbnail", {public = true})

    target("autostart")
        set_kind("object")
        add_deps("rd_log")
        add_files("src/autostart/*.cpp")
        add_includedirs("src/autostart", {public = true})

    target("config_center")
        set_kind("object")
        add_deps("rd_log", "autostart")
        add_files("src/config_center/*.cpp")
        add_includedirs("src/config_center", {public = true})

    target("assets")
        set_kind("headeronly")
        add_includedirs("src/gui/assets/localization",
            "src/gui/assets/fonts",
            "src/gui/assets/icons",
            "src/gui/assets/layouts", {public = true})

    target("version_checker")
        set_kind("object")
        add_packages("cpp-httplib")
        add_defines("CROSSDESK_VERSION=\"" .. (get_config("CROSSDESK_VERSION") or "Unknown") .. "\"")
        add_deps("rd_log")
        add_files("src/version_checker/*.cpp")
        add_includedirs("src/version_checker", {public = true})
        if is_os("macosx") then
            add_defines("CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN")
            add_frameworks("Security", "CoreFoundation")
        end

    target("tools")
        set_kind("object")
        add_deps("rd_log")
        add_files("src/tools/*.cpp")
        if is_os("macosx") then
            add_files("src/tools/*.mm")
        end
        add_includedirs("src/tools", {public = true})

    target("gui")
        set_kind("object")
        add_packages("libyuv", "tinyfiledialogs")
        add_defines("CROSSDESK_VERSION=\"" .. (get_config("CROSSDESK_VERSION") or "Unknown") .. "\"")
        add_deps("rd_log", "common", "assets", "config_center", "minirtc",
            "path_manager", "screen_capturer", "speaker_capturer",
            "device_controller", "thumbnail", "version_checker", "tools")
        add_files("src/gui/render.cpp", "src/gui/application/*.cpp",
            "src/gui/runtime/*.cpp",
            "src/gui/features/devices/*.cpp", "src/gui/features/input/*.cpp",
            "src/gui/features/clipboard/*.cpp", "src/gui/features/file_transfer/*.cpp",
            "src/gui/features/settings/*.cpp", "src/gui/views/panels/*.cpp",
            "src/gui/views/toolbars/*.cpp", "src/gui/views/windows/*.cpp")
        add_includedirs("src/gui", {public = true})
        if is_os("windows") then
            add_files("src/gui/platform/tray/win_tray.cpp")
            add_includedirs("src/service/windows", {public = true})
        elseif is_os("macosx") then
            add_files("src/gui/runtime/*.mm", "src/gui/views/windows/*.mm",
                "src/gui/platform/tray/*.mm")
        elseif is_os("linux") then
            add_files("src/gui/platform/tray/linux_tray.cpp")
        end

    if is_os("windows") then
        target("wgc_plugin")
            set_kind("shared")
            add_packages("libyuv")
            add_deps("rd_log", "path_manager")
            add_defines("CROSSDESK_WGC_PLUGIN_BUILD=1")
            -- Keep the project on C++17 while C++/WinRT still falls back to
            -- MSVC's deprecated experimental coroutine header.
            add_defines("_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS")
            add_links("windowsapp")
            add_files("src/screen_capturer/windows/screen_capturer_wgc.cpp",
                "src/screen_capturer/windows/wgc_session_impl.cpp",
                "src/screen_capturer/windows/wgc_plugin_entry.cpp")
            add_includedirs("src/common", "src/screen_capturer",
                "src/screen_capturer/windows")

        target("crossdesk_service")
            set_kind("binary")
            add_deps("rd_log", "path_manager")
            add_links("Advapi32", "Wtsapi32", "Ole32", "Userenv")
            add_files("src/service/windows/main.cpp",
                "src/service/windows/service_host.cpp")
            add_includedirs("src/service/windows", {public = true})

        target("crossdesk_session_helper")
            set_kind("binary")
            add_packages("libyuv")
            add_deps("rd_log", "path_manager")
            add_links("Advapi32", "User32", "Wtsapi32", "Gdi32")
            add_files("src/service/windows/session_helper_main.cpp")
            add_files(crossdesk_windows_resource)
            add_includedirs("src/service/windows", {public = true})
    end

    target("crossdesk")
        set_kind("binary")
        add_deps("rd_log", "common", "gui")
        add_files("src/app/*.cpp")
        add_includedirs("src/app", {public = true})
        if is_os("windows") then
            add_files("src/service/windows/service_host.cpp")
            add_includedirs("src/service/windows", {public = true})
            add_links("Advapi32", "Wtsapi32", "Ole32", "Userenv")
            add_deps("wgc_plugin", "crossdesk_service", "crossdesk_session_helper")
            add_files(crossdesk_windows_resource)
        end
end
