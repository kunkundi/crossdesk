function setup_targets()
    add_packages("spdlog", "libsdl3", "nlohmann_json")

    includes("deps/submodules", "deps/thirdparty")

    local function copy_slint_runtime(target)
        if not target:is_plat("windows") then
            return
        end

        local slint = target:pkg("slint")
        if not slint and target:dep("gui") then
            slint = target:dep("gui"):pkg("slint")
        end
        assert(slint, "the Slint package is required to copy its Windows runtime")
        local runtime_dir = path.join(slint:installdir(), "lib")
        local runtime_dll = path.join(runtime_dir, "slint_cpp.dll")
        assert(os.isfile(runtime_dll), "Slint runtime not found: " .. runtime_dll)
        os.cp(runtime_dll, target:targetdir())
    end

    local crossdesk_windows_resource = "apps/desktop/resources/windows/crossdesk.rc"
    if is_config("CROSSDESK_PORTABLE", true) then
        crossdesk_windows_resource = "apps/desktop/resources/windows/crossdesk_portable.rc"
    end

    target("rd_log")
        set_kind("object")
        add_packages("spdlog")
        add_files("apps/desktop/src/log/rd_log.cpp")
        add_includedirs("apps/desktop/src/log", {public = true})

    target("common")
        set_kind("object")
        add_deps("rd_log", "crossdesk_wire")
        add_packages("libyuv")
        add_files("apps/desktop/src/common/*.cpp")
        remove_files("apps/desktop/src/common/rounded_corner_button.cpp")
        if is_os("windows") then
            add_files("apps/desktop/src/platform/windows/system_info.cpp")
        elseif is_os("macosx") then
            add_files("apps/desktop/src/platform/macos/system_info.cpp")
        elseif is_os("linux") then
            add_files("apps/desktop/src/platform/linux/system_info.cpp",
                "apps/desktop/src/platform/linux/common/*.cpp")
            add_includedirs("apps/desktop/src/platform/linux/common")
        end
        add_includedirs("apps/desktop/src/common", {public = true})

    target("path_manager")
        set_kind("object")
        add_deps("rd_log")
        add_files("apps/desktop/src/path_manager/*.cpp")
        if is_os("windows") then
            add_files("apps/desktop/src/platform/windows/path_backend.cpp")
        elseif is_os("macosx") then
            add_files("apps/desktop/src/platform/macos/path_backend.cpp")
        elseif is_os("linux") then
            add_files("apps/desktop/src/platform/linux/path_backend.cpp")
        end
        add_includedirs("apps/desktop/src", {public = true})
        add_includedirs("apps/desktop/src/path_manager", {public = true})

    target("path_manager_portable_test")
        set_kind("binary")
        set_default(false)
        set_policy("build.ccache", false)
        add_defines("CROSSDESK_PORTABLE=1")
        add_includedirs("apps/desktop/src", "apps/desktop/src/path_manager")
        add_files("apps/desktop/tests/path_manager_portable_test.cpp",
            "apps/desktop/src/path_manager/path_manager.cpp")
        if is_os("windows") then
            add_files("apps/desktop/src/platform/windows/path_backend.cpp")
        elseif is_os("macosx") then
            add_files("apps/desktop/src/platform/macos/path_backend.cpp")
        elseif is_os("linux") then
            add_files("apps/desktop/src/platform/linux/path_backend.cpp")
        end

    target("macos_keyboard_modifier_state_test")
        set_kind("binary")
        set_default(false)
        add_includedirs("apps/desktop/src/platform/macos/input")
        add_files("apps/desktop/tests/macos_keyboard_modifier_state_test.cpp")

    target("cursor_position_test")
        set_kind("binary")
        set_default(false)
        add_deps("crossdesk_wire")
        add_includedirs("apps/desktop/src/common",
            "apps/desktop/src/gui/runtime")
        add_files("apps/desktop/tests/cursor_position_test.cpp")

    target("repository_structure_test")
        set_kind("binary")
        set_default(false)
        add_files("apps/desktop/tests/repository_structure_test.cpp")

    target("connection_status_protocol_test")
        set_kind("binary")
        set_default(false)
        add_files("apps/desktop/tests/connection_status_protocol_test.cpp")

    target("video_callback_lifetime_test")
        set_kind("binary")
        set_default(false)
        add_files("apps/desktop/tests/video_callback_lifetime_test.cpp")

    target("windows_manifest_resource_test")
        set_kind("binary")
        set_default(false)
        add_files("apps/desktop/tests/windows_manifest_resource_test.cpp")

    target("windows_service_mouse_ipc_test")
        set_kind("binary")
        set_default(false)
        add_files("apps/desktop/tests/windows_service_mouse_ipc_test.cpp")

    target("windows_mouse_controller_safety_test")
        set_kind("binary")
        set_default(false)
        add_files("apps/desktop/tests/windows_mouse_controller_safety_test.cpp")

    target("windows_sas_guard_test")
        set_kind("binary")
        set_default(false)
        add_includedirs("apps/desktop/src/platform/windows/service")
        add_files("apps/desktop/tests/windows_sas_guard_test.cpp")

    target("slint_ui_smoke_test")
        set_kind("binary")
        set_languages("c++20")
        set_default(false)
        if is_os("windows") then
            add_cxxflags("/bigobj")
        end
        add_packages("slint")
        add_includedirs("apps/desktop/src/gui", "apps/desktop/src/gui/assets/fonts",
            "apps/desktop/src/gui/assets/localization")
        add_rules("slint")
        add_files("apps/desktop/src/gui/ui/crossdesk_ui.slint")
        add_files("apps/desktop/tests/slint_ui_smoke_test.cpp")
        after_build(copy_slint_runtime)

    target("version_checker_test")
        set_kind("binary")
        set_default(false)
        add_packages("cpp-httplib")
        add_deps("rd_log")
        add_includedirs("apps/desktop/src/version_checker")
        add_files("apps/desktop/tests/version_checker_test.cpp",
            "apps/desktop/src/version_checker/version_checker.cpp")
        if is_os("macosx") then
            add_defines("CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN")
            add_frameworks("Security", "CoreFoundation")
        end

    target("screen_capturer")
        set_kind("object")
        add_deps("rd_log", "common", "crossdesk_wire")
        add_includedirs("apps/desktop/src/screen_capturer", {public = true})
        add_includedirs("deps/submodules/minirtc/src/api", {public = true})
        if is_os("windows") then
            add_packages("libyuv")
            add_files("apps/desktop/src/screen_capturer/captured_nv12_frame.cpp")
            add_files("apps/desktop/src/platform/windows/screen_capturer/screen_capturer_dxgi.cpp",
                "apps/desktop/src/platform/windows/screen_capturer/screen_capturer_gdi.cpp",
                "apps/desktop/src/platform/windows/screen_capturer/screen_capturer_win.cpp",
                "apps/desktop/src/platform/windows/screen_capturer/screen_capturer_factory.cpp")
            add_includedirs("apps/desktop/src/platform/windows/screen_capturer",
                "apps/desktop/src/platform/windows/service")
        elseif is_os("macosx") then
            add_files("apps/desktop/src/platform/macos/screen_capturer/*.cpp",
                "apps/desktop/src/platform/macos/screen_capturer/*.mm")
            add_includedirs("apps/desktop/src/platform/macos/screen_capturer")
        elseif is_os("linux") then
            add_packages("libyuv")
            add_files("apps/desktop/src/platform/linux/screen_capturer/screen_capturer_linux.cpp")
            add_files("apps/desktop/src/platform/linux/screen_capturer/screen_capturer_x11.cpp")
            add_files("apps/desktop/src/platform/linux/screen_capturer/screen_capturer_drm.cpp")
            add_files("apps/desktop/src/platform/linux/screen_capturer/screen_capturer_factory.cpp")
            if is_config("USE_WAYLAND", true) then
                add_files("apps/desktop/src/platform/linux/screen_capturer/screen_capturer_wayland.cpp")
                add_files("apps/desktop/src/platform/linux/screen_capturer/screen_capturer_wayland_portal.cpp")
                add_files("apps/desktop/src/platform/linux/screen_capturer/screen_capturer_wayland_pipewire.cpp")
            end
            add_includedirs("apps/desktop/src/platform/linux/screen_capturer",
                "apps/desktop/src/platform/linux/common")
        end

    target("speaker_capturer")
        set_kind("object")
        add_deps("rd_log", "crossdesk_wire")
        add_includedirs("apps/desktop/src/speaker_capturer", {public = true})
        if is_os("windows") then
            add_packages("miniaudio")
            add_files("apps/desktop/src/platform/windows/speaker_capturer/*.cpp")
            add_includedirs("apps/desktop/src/platform/windows/speaker_capturer")
        elseif is_os("macosx") then
            add_files("apps/desktop/src/platform/macos/speaker_capturer/*.cpp",
                "apps/desktop/src/platform/macos/speaker_capturer/*.mm")
            add_includedirs("apps/desktop/src/platform/macos/speaker_capturer")
        elseif is_os("linux") then
            add_files("apps/desktop/src/platform/linux/speaker_capturer/*.cpp")
            add_includedirs("apps/desktop/src/platform/linux/speaker_capturer")
        end

    target("device_controller")
        set_kind("object")
        add_deps("rd_log", "common", "crossdesk_wire")
        add_includedirs("apps/desktop/src/device_controller", {public = true})
        add_includedirs("apps/desktop/src/platform/common/input")
        if is_os("windows") then
            add_files("apps/desktop/src/platform/windows/input/mouse/*.cpp",
                "apps/desktop/src/platform/windows/input/keyboard/*.cpp",
                "apps/desktop/src/platform/windows/input/device_controller_factory.cpp")
            add_includedirs("apps/desktop/src/platform/windows/input/mouse",
                "apps/desktop/src/platform/windows/input/keyboard",
                "apps/desktop/src/platform/windows/input")
        elseif is_os("macosx") then
            add_files("apps/desktop/src/platform/macos/input/mouse/*.cpp",
                "apps/desktop/src/platform/macos/input/keyboard/*.cpp",
                "apps/desktop/src/platform/macos/input/device_controller_factory.cpp")
            add_includedirs("apps/desktop/src/platform/macos/input/mouse",
                "apps/desktop/src/platform/macos/input/keyboard",
                "apps/desktop/src/platform/macos/input")
        elseif is_os("linux") then
            add_files("apps/desktop/src/platform/linux/input/mouse/*.cpp",
                "apps/desktop/src/platform/linux/input/keyboard/*.cpp",
                "apps/desktop/src/platform/linux/input/device_controller_factory.cpp")
            add_includedirs("apps/desktop/src/platform/linux/input/mouse",
                "apps/desktop/src/platform/linux/input/keyboard",
                "apps/desktop/src/platform/linux/input",
                "apps/desktop/src/platform/linux/common")
        end

    if is_os("linux") then
        target("linux_keyboard_x11_integration_test")
            set_kind("binary")
            set_default(false)
            add_deps("device_controller")
            add_files("apps/desktop/tests/linux_keyboard_x11_integration_test.cpp")
    end

    target("thumbnail")
        set_kind("object")
        add_packages("libyuv", "openssl3")
        add_deps("rd_log", "common")
        add_files("apps/desktop/src/thumbnail/*.cpp")
        add_includedirs("apps/desktop/src/thumbnail", {public = true})

    target("autostart")
        set_kind("object")
        add_deps("rd_log")
        add_files("apps/desktop/src/autostart/*.cpp")
        if is_os("windows") then
            add_files("apps/desktop/src/platform/windows/autostart.cpp")
        elseif is_os("macosx") then
            add_files("apps/desktop/src/platform/macos/autostart.cpp")
        elseif is_os("linux") then
            add_files("apps/desktop/src/platform/linux/autostart.cpp")
        end
        add_includedirs("apps/desktop/src")
        add_includedirs("apps/desktop/src/autostart", {public = true})

    target("config_center")
        set_kind("object")
        add_deps("rd_log", "autostart")
        add_files("apps/desktop/src/config_center/*.cpp")
        add_includedirs("apps/desktop/src/config_center", {public = true})

    target("assets")
        set_kind("headeronly")
        add_includedirs("apps/desktop/src/gui/assets/localization",
            "apps/desktop/src/gui/assets/fonts",
            "apps/desktop/src/gui/assets/icons",
            "apps/desktop/src/gui/assets/layouts", {public = true})

    target("version_checker")
        set_kind("object")
        add_packages("cpp-httplib")
        add_defines("CROSSDESK_VERSION=\"" .. (get_config("CROSSDESK_VERSION") or "Unknown") .. "\"")
        add_deps("rd_log")
        add_files("apps/desktop/src/version_checker/*.cpp")
        add_includedirs("apps/desktop/src/version_checker", {public = true})
        if is_os("macosx") then
            add_defines("CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN")
            add_frameworks("Security", "CoreFoundation")
        end

    target("tools")
        set_kind("object")
        add_deps("rd_log", "common", "crossdesk_wire")
        add_files("apps/desktop/src/tools/*.cpp")
        if is_os("windows") then
            add_files("apps/desktop/src/platform/windows/clipboard.cpp")
        elseif is_os("macosx") then
            add_files("apps/desktop/src/platform/macos/clipboard.mm")
        elseif is_os("linux") then
            add_files("apps/desktop/src/platform/linux/clipboard.cpp")
        end
        add_includedirs("apps/desktop/src", "apps/desktop/src/tools", {public = true})

    target("gui")
        set_kind("object")
        set_languages("c++20")
        -- spdlog 1.14 bundles fmt 10, whose consteval parser is rejected by
        -- current Apple Clang in C++20 mode. Keep the established dependency
        -- version and use fmt's supported runtime-parser fallback here.
        add_defines("FMT_CONSTEVAL=")
        add_packages("slint", {public = true})
        add_packages("libyuv", "tinyfiledialogs")
        add_rules("slint")
        add_defines("CROSSDESK_VERSION=\"" .. (get_config("CROSSDESK_VERSION") or "Unknown") .. "\"")
        add_deps("rd_log", "common", "assets", "config_center", "minirtc",
            "path_manager", "screen_capturer", "speaker_capturer",
            "device_controller", "thumbnail", "version_checker", "tools",
            "crossdesk_wire")
        add_files("apps/desktop/src/gui/render.cpp", "apps/desktop/src/gui/application/gui_application.cpp",
            "apps/desktop/src/gui/rendering/*.cpp",
            "apps/desktop/src/gui/runtime/*.cpp",
            "apps/desktop/src/platform/common/gui/service_status_runtime.cpp",
            "apps/desktop/src/gui/features/devices/*.cpp", "apps/desktop/src/gui/features/input/*.cpp",
            "apps/desktop/src/gui/features/clipboard/*.cpp", "apps/desktop/src/gui/features/file_transfer/*.cpp",
            "apps/desktop/src/gui/features/settings/*.cpp", "apps/desktop/src/gui/ui/crossdesk_ui.slint")
        add_includedirs("apps/desktop/src", "apps/desktop/src/gui", {public = true})
        add_includedirs("apps/desktop/src/platform/common/input")
        if is_os("windows") then
            add_cxxflags("/bigobj")
            add_links("opengl32")
            add_files("apps/desktop/src/platform/windows/gui/tray/win_tray.cpp",
                "apps/desktop/src/platform/windows/gui/slint_backend.cpp",
                "apps/desktop/src/platform/windows/gui/slint_renderer_probe.cpp",
                "apps/desktop/src/platform/common/gui/opengl_video_renderer.cpp",
                "apps/desktop/src/platform/common/gui/video_renderer_factory_opengl.cpp",
                "apps/desktop/src/platform/windows/gui/runtime/windows_service_runtime.cpp",
                "apps/desktop/src/platform/windows/gui/application/portable_service_integration.cpp")
            add_includedirs("apps/desktop/src/platform/common/gui",
                "apps/desktop/src/platform/windows/gui/tray")
            add_includedirs("apps/desktop/src/platform/windows/service")
        elseif is_os("macosx") then
            add_files("apps/desktop/src/platform/macos/gui/runtime/*.mm",
                "apps/desktop/src/platform/macos/gui/tray/*.mm",
                "apps/desktop/src/platform/macos/gui/window_drag_mac.mm",
                "apps/desktop/src/platform/macos/gui/metal_video_renderer.mm",
                "apps/desktop/src/platform/macos/gui/video_renderer_factory_metal.mm")
            add_includedirs("apps/desktop/src/platform/macos/gui",
                "apps/desktop/src/platform/macos/gui/tray")
        elseif is_os("linux") then
            add_links("GL")
            add_files("apps/desktop/src/platform/linux/gui/tray/linux_tray.cpp",
                "apps/desktop/src/platform/common/gui/opengl_video_renderer.cpp",
                "apps/desktop/src/platform/common/gui/video_renderer_factory_opengl.cpp")
            add_includedirs("apps/desktop/src/platform/common/gui",
                "apps/desktop/src/platform/linux/gui/tray",
                "apps/desktop/src/platform/linux/common")
        end

    if is_os("windows") then
        target("wgc_plugin")
            set_kind("shared")
            add_packages("libyuv")
            add_deps("rd_log", "path_manager", "crossdesk_wire")
            add_defines("CROSSDESK_WGC_PLUGIN_BUILD=1")
            -- Keep the project on C++17 while C++/WinRT still falls back to
            -- MSVC's deprecated experimental coroutine header.
            add_defines("_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS")
            add_links("windowsapp")
            add_files("apps/desktop/src/platform/windows/screen_capturer/screen_capturer_wgc.cpp",
                "apps/desktop/src/platform/windows/screen_capturer/wgc_session_impl.cpp",
                "apps/desktop/src/platform/windows/screen_capturer/wgc_plugin_entry.cpp")
            add_files("apps/desktop/resources/windows/wgc_plugin.rc")
            add_includedirs("apps/desktop/src/common", "apps/desktop/src/screen_capturer",
                "apps/desktop/src/platform/windows/screen_capturer",
                "deps/submodules/minirtc/src/api")

        target("crossdesk_service")
            set_kind("binary")
            add_deps("rd_log", "path_manager")
            add_links("Advapi32", "Wtsapi32", "Ole32", "Userenv")
            add_files("apps/desktop/src/platform/windows/service/main.cpp",
                "apps/desktop/src/platform/windows/service/service_host.cpp")
            add_files("apps/desktop/resources/windows/crossdesk_service.rc")
            add_includedirs("apps/desktop/src/platform/windows/service")

        target("crossdesk_session_helper")
            set_kind("binary")
            add_packages("libyuv")
            add_deps("rd_log", "path_manager")
            add_links("Advapi32", "User32", "Wtsapi32", "Gdi32")
            add_files("apps/desktop/src/platform/windows/service/session_helper_main.cpp")
            add_files("apps/desktop/resources/windows/crossdesk_session_helper.rc")
            add_includedirs("apps/desktop/src/common",
                "apps/desktop/src/platform/windows/service")
            add_includedirs("apps/desktop/src/platform/windows/input")
    end

    target("crossdesk")
        set_kind("binary")
        add_deps("rd_log", "common", "gui")
        add_files("apps/desktop/src/app/*.cpp")
        add_includedirs("apps/desktop/src", "apps/desktop/src/app", {public = true})
        if is_os("windows") then
            add_files("apps/desktop/src/platform/windows/daemon_backend.cpp")
            add_files("apps/desktop/src/platform/windows/service/service_host.cpp")
            add_includedirs("apps/desktop/src/platform/windows/service")
            add_links("Advapi32", "Wtsapi32", "Ole32", "Userenv")
            add_deps("wgc_plugin", "crossdesk_service", "crossdesk_session_helper")
            add_files(crossdesk_windows_resource)
        elseif is_os("macosx") then
            add_files("apps/desktop/src/platform/common/posix_daemon_backend.cpp",
                "apps/desktop/src/platform/macos/daemon_backend.cpp")
        elseif is_os("linux") then
            add_files("apps/desktop/src/platform/common/posix_daemon_backend.cpp",
                "apps/desktop/src/platform/linux/daemon_backend.cpp")
        end
        after_build(copy_slint_runtime)
end
