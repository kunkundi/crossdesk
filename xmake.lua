set_project("crossdesk")
set_license("LGPL-3.0")

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

add_rules("mode.release", "mode.debug")
set_languages("c++17")
set_encodings("utf-8")

-- set_policy("build.warning", true)
-- set_warnings("all", "extra")
-- add_cxxflags("/W4", "/WX")

add_defines("UNICODE")
add_defines("USE_CUDA=" .. (is_config("USE_CUDA", true) and "1" or "0"))

if is_mode("debug") then
    add_defines("CROSSDESK_DEBUG")
end

add_requires("spdlog 1.14.1", {system = false})
add_requires("imgui v1.92.1-docking", {configs = {sdl3 = true, sdl3_renderer = true}})
add_requires("openssl3 3.3.2", {system = false})
add_requires("nlohmann_json 3.11.3")
add_requires("cpp-httplib v0.26.0", {configs = {ssl = true}})
add_requires("tinyfiledialogs 3.15.1")

if is_os("windows") then
    add_requires("libyuv", "miniaudio 0.11.21")
    add_links("Shell32", "windowsapp", "dwmapi", "User32", "kernel32",
        "SDL3-static", "gdi32", "winmm", "setupapi", "version",
        "Imm32", "iphlpapi")
    add_cxflags("/WX")
    set_runtimes("MT")
elseif is_os("linux") then
    add_links("pulse-simple", "pulse")
    add_requires("libyuv") 
    add_syslinks("pthread", "dl")
    add_links("SDL3", "asound", "X11", "Xtst", "Xrandr", "Xfixes")
    add_cxflags("-Wno-unused-variable")   
elseif is_os("macosx") then
    add_links("SDL3")
    add_ldflags("-Wl,-ld_classic")
    add_cxflags("-Wno-unused-variable")
    add_frameworks("OpenGL", "IOSurface", "ScreenCaptureKit", "AVFoundation", 
        "CoreMedia", "CoreVideo", "CoreAudio", "AudioToolbox")
end

add_packages("spdlog", "imgui", "nlohmann_json")

includes("submodules", "thirdparty")

target("rd_log")
    set_kind("object")
    add_packages("spdlog")
    add_files("src/log/rd_log.cpp")
    add_includedirs("src/log", {public = true})

target("common")
    set_kind("object")
    add_deps("rd_log")
    add_files("src/common/*.cpp")
    add_includedirs("src/common", {public = true})

target("path_manager")
    set_kind("object")
    add_deps("rd_log")
    add_includedirs("src/path_manager", {public = true})
    add_files("src/path_manager/*.cpp")
    add_includedirs("src/path_manager", {public = true})

target("screen_capturer")
    set_kind("object")
    add_deps("rd_log", "common")
    add_includedirs("src/screen_capturer", {public = true})
    if is_os("windows") then
        add_packages("libyuv")
        add_files("src/screen_capturer/windows/*.cpp")
        add_includedirs("src/screen_capturer/windows", {public = true})
    elseif is_os("macosx") then
        add_files("src/screen_capturer/macosx/*.cpp",
        "src/screen_capturer/macosx/*.mm")
        add_includedirs("src/screen_capturer/macosx", {public = true})
    elseif is_os("linux") then
        add_packages("libyuv")
        add_files("src/screen_capturer/linux/*.cpp")
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

target("tools")
    set_kind("object")
    add_deps("rd_log")
    add_files("src/tools/*.cpp")
    add_includedirs("src/tools", {public = true})

target("gui")
    set_kind("object")
    add_packages("libyuv", "tinyfiledialogs")
    add_defines("CROSSDESK_VERSION=\"" .. (get_config("CROSSDESK_VERSION") or "Unknown") .. "\"")
    add_deps("rd_log", "common", "assets", "config_center", "minirtc", 
        "path_manager", "screen_capturer", "speaker_capturer", 
        "device_controller", "thumbnail", "version_checker", "tools")
    add_files("src/gui/*.cpp", "src/gui/panels/*.cpp", "src/gui/toolbars/*.cpp",
        "src/gui/windows/*.cpp")
    add_includedirs("src/gui", "src/gui/panels", "src/gui/toolbars",
        "src/gui/windows", {public = true})
    if is_os("windows") then
        add_files("src/gui/tray/*.cpp")
        add_includedirs("src/gui/tray", {public = true})
    elseif is_os("macosx") then
        add_files("src/gui/windows/*.mm")
    end

target("crossdesk")
    set_kind("binary")
    add_deps("rd_log", "common", "gui")
    add_files("src/app/*.cpp")
    add_includedirs("src/app", {public = true})