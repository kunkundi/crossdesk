set_project("crossdesk")
set_license("LGPL-3.0")

set_version("0.0.1")
add_defines("RD_VERSION=\"0.0.1\"");

add_rules("mode.release", "mode.debug")
set_languages("c++17")
set_encodings("utf-8")

-- set_policy("build.warning", true)
-- set_warnings("all", "extra")

add_defines("UNICODE")
if is_mode("debug") then
    add_defines("DESK_PORT_DEBUG")
end

add_requires("spdlog 1.14.1", {system = false})
add_requires("imgui v1.91.5-docking", {configs = {sdl3 = true, sdl3_renderer = true}})
add_requires("openssl3 3.3.2", {system = false})

if is_os("windows") then
    add_requires("libyuv", "miniaudio 0.11.21")
    add_links("Shell32", "windowsapp", "dwmapi", "User32", "kernel32",
        "SDL3-static", "gdi32", "winmm", "setupapi", "version",
        "Imm32", "iphlpapi")
    add_cxflags("/WX")
elseif is_os("linux") then
    add_links("pulse-simple", "pulse")
    add_requires("libyuv") 
    add_syslinks("pthread", "dl")
    add_links("SDL3", "asound", "X11", "Xtst", "Xrandr")
    add_cxflags("-Wno-unused-variable")   
elseif is_os("macosx") then
    add_links("SDL3")
    add_ldflags("-Wl,-ld_classic")
    add_cxflags("-Wno-unused-variable")
    add_frameworks("OpenGL", "IOSurface", "ScreenCaptureKit", "AVFoundation", 
        "CoreMedia", "CoreVideo", "CoreAudio", "AudioToolbox")
end

add_packages("spdlog", "imgui")

includes("thirdparty")

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

target("config_center")
    set_kind("object")
    add_deps("rd_log")
    add_files("src/config_center/*.cpp")
    add_includedirs("src/config_center", {public = true})

target("localization")
    set_kind("headeronly")
    add_includedirs("src/localization", {public = true})

target("single_window")
    set_kind("object")
    add_packages("libyuv", "openssl3")
    add_deps("rd_log", "common", "localization", "config_center", "minirtc", 
        "path_manager", "screen_capturer", "speaker_capturer", "device_controller")
    add_files("src/single_window/*.cpp")
    add_includedirs("src/single_window", {public = true})
    add_includedirs("fonts", {public = true})

target("crossdesk")
    set_kind("binary")
    add_deps("rd_log", "common", "single_window")
    if is_os("windows") then
        add_files("icons/app.rc")
    elseif is_os("macosx") then
        -- add_rules("xcode.application")
        -- add_files("Info.plist")
    elseif is_os("linux") then
    end
    add_files("src/gui/main.cpp")