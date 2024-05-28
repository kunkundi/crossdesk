set_project("remote_desk")
set_version("0.0.1")
set_license("LGPL-3.0")

add_rules("mode.release", "mode.debug")
set_languages("c++17")

-- set_policy("build.warning", true)
-- set_warnings("all", "extra")

add_defines("UNICODE")
if is_mode("debug") then
    add_defines("REMOTE_DESK_DEBUG")
end

add_requires("spdlog 1.14.1", {system = false})
add_requires("imgui 1.90.6", {configs = {sdl2 = true, sdl2_renderer = true}})
add_requires("libyuv")

if is_os("windows") then
    add_links("Shell32", "windowsapp", "dwmapi", "User32", "kernel32",
        "SDL2-static", "SDL2main", "gdi32", "winmm", "setupapi", "version",
        "Imm32", "iphlpapi")
elseif is_os("linux") then
    add_requires("ffmpeg 5.1.2", {system = false})
    add_syslinks("pthread", "dl")
    add_linkdirs("thirdparty/projectx/thirdparty/nvcodec/Lib/x64")
    add_links("SDL2", "cuda", "nvidia-encode", "nvcuvid")
    add_ldflags("-lavformat", "-lavdevice", "-lavfilter", "-lavcodec",
        "-lswscale", "-lavutil", "-lswresample",
        "-lasound", "-lxcb-shape", "-lxcb-xfixes", "-lsndio", "-lxcb", 
        "-lxcb-shm", "-lXext", "-lX11", "-lXv", "-ldl", "-lpthread",
        {force = true})
elseif is_os("macosx") then
    add_requires("ffmpeg 5.1.2", {system = false})
    add_requires("libxcb", {system = false})
    add_packages("libxcb")
    add_links("SDL2", "SDL2main")
    add_ldflags("-Wl,-ld_classic")
    add_frameworks("OpenGL")
end

add_packages("spdlog", "imgui")

includes("thirdparty")

target("log")
    set_kind("object")
    add_packages("spdlog")
    add_headerfiles("src/log/log.h")
    add_includedirs("src/log", {public = true})

target("common")
    set_kind("object")
    add_deps("log")
    add_files("src/common/*.cpp")
    add_includedirs("src/common", {public = true})

target("screen_capturer")
    set_kind("object")
    add_deps("log")
    add_packages("libyuv")
    add_includedirs("src/screen_capturer", {public = true})
    if is_os("windows") then
        add_files("src/screen_capturer/windows/*.cpp")
        add_includedirs("src/screen_capturer/windows", {public = true})
    elseif is_os("macosx") then
        add_packages("ffmpeg")
        add_files("src/screen_capturer/macosx/*.cpp")
        add_includedirs("src/screen_capturer/macosx", {public = true})
    elseif is_os("linux") then
        add_packages("ffmpeg")
        add_files("src/screen_capturer/linux/*.cpp")
        add_includedirs("src/screen_capturer/linux", {public = true})
    end

target("device_controller")
    set_kind("object")
    add_deps("log")
    add_includedirs("src/device_controller", {public = true})
    if is_os("windows") then
        add_files("src/device_controller/mouse/windows/*.cpp")
        add_includedirs("src/device_controller/mouse/windows", {public = true})
    elseif is_os("macosx") then
        add_files("src/device_controller/mouse/mac/*.cpp")
        add_includedirs("src/device_controller/mouse/mac", {public = true})
    elseif is_os("linux") then
         add_files("src/device_controller/mouse/linux/*.cpp")
         add_includedirs("src/device_controller/mouse/linux", {public = true})
    end

target("remote_desk")
    set_kind("binary")
    add_deps("log", "common", "screen_capturer", "device_controller", "projectx")
    if is_os("macosx") then
        add_packages("ffmpeg")
    elseif is_os("linux") then
        add_packages("ffmpeg")
    end
    add_files("src/gui/main.cpp")

    -- after_install(function (target)
    --     os.cp("$(projectdir)/thirdparty/nvcodec/Lib/x64/*.so", "$(projectdir)/out/bin")
    --     os.cp("$(projectdir)/thirdparty/nvcodec/Lib/x64/*.so.1", "$(projectdir)/out/bin")
    --     os.cp("$(projectdir)/out/lib/*.so", "$(projectdir)/out/bin")
    --     os.rm("$(projectdir)/out/include")
    --     os.rm("$(projectdir)/out/lib")
    -- end)

-- target("screen_capturer")
--     set_kind("binary")
--     add_packages("sdl2", "imgui",  "ffmpeg", "openh264")
--     add_files("test/screen_capturer/linux_capture.cpp")
--     add_ldflags("-lavformat", "-lavdevice", "-lavfilter", "-lavcodec",
--     "-lswscale", "-lavutil", "-lswresample",
--     "-lasound", "-lxcb-shape", "-lxcb-xfixes", "-lsndio", "-lxcb", 
--     "-lxcb-shm", "-lXext", "-lX11", "-lXv", "-lpthread", "-lSDL2", "-lopenh264",
--     "-ldl", {force = true})

-- target("screen_capturer")
--     set_kind("binary")
--     add_packages("sdl2", "imgui",  "ffmpeg", "openh264")
--     add_files("test/screen_capturer/mac_capture.cpp")
--     add_ldflags("-lavformat", "-lavdevice", "-lavfilter", "-lavcodec",
--     "-lswscale", "-lavutil", "-lswresample",
--     "-lasound", "-lxcb-shape", "-lxcb-xfixes", "-lsndio", "-lxcb", 
--     "-lxcb-shm", "-lXext", "-lX11", "-lXv", "-lpthread", "-lSDL2", "-lopenh264",
--     "-ldl", {force = true})

-- target("audio_capture")
--     set_kind("binary")
--     add_packages("ffmpeg")
--     add_files("test/audio_capture/sdl2_audio_capture.cpp")
--     add_includedirs("test/audio_capture")
--     add_ldflags("-lavformat", "-lavdevice", "-lavfilter", "-lavcodec",
--     "-lswscale", "-lavutil", "-lswresample",
--     "-lasound", "-lxcb-shape", "-lxcb-xfixes", "-lsndio", "-lxcb", 
--     "-lxcb-shm", "-lXext", "-lX11", "-lXv", "-lpthread", "-lSDL2", "-lopenh264",
--     "-ldl", {force = true})
--     add_links("Shlwapi", "Strmiids", "Vfw32", "Secur32", "Mfuuid")

-- target("play_audio")
--     set_kind("binary")
--     add_packages("ffmpeg")
--     add_files("test/audio_capture/play_loopback.cpp")
--     add_includedirs("test/audio_capture")
--     add_ldflags("-lavformat", "-lavdevice", "-lavfilter", "-lavcodec",
--     "-lswscale", "-lavutil", "-lswresample",
--     "-lasound", "-lxcb-shape", "-lxcb-xfixes", "-lsndio", "-lxcb", 
--     "-lxcb-shm", "-lXext", "-lX11", "-lXv", "-lpthread", "-lSDL2", "-lopenh264",
--     "-ldl", {force = true})
--     add_links("Shlwapi", "Strmiids", "Vfw32", "Secur32", "Mfuuid")

-- target("audio_capture")
--     set_kind("binary")
--     add_packages("libopus")
--     add_files("test/audio_capture/sdl2_audio_capture.cpp")
--     add_includedirs("test/audio_capture")
--     add_ldflags("-lavformat", "-lavdevice", "-lavfilter", "-lavcodec",
--     "-lswscale", "-lavutil", "-lswresample",
--     "-lasound", "-lxcb-shape", "-lxcb-xfixes", "-lsndio", "-lxcb", 
--     "-lxcb-shm", "-lXext", "-lX11", "-lXv", "-lpthread", "-lSDL2", "-lopenh264",
--     "-ldl", {force = true})

-- target("mouse_control")
--     set_kind("binary")
--     add_files("test/linux_mouse_control/mouse_control.cpp")
--     add_includedirs("test/linux_mouse_control")