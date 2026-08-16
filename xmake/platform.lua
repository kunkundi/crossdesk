local function add_existing_include_dirs(paths, opts)
    for _, dir in ipairs(paths) do
        if os.isdir(dir) then
            add_includedirs(dir, opts)
        end
    end
end

local function collect_dbus_arch_include_dirs()
    local include_dirs = {}
    for _, pattern in ipairs({
            "/usr/lib/*/dbus-1.0/include",
            "/usr/lib64/dbus-1.0/include",
            "/usr/lib/dbus-1.0/include",
            "/lib/*/dbus-1.0/include",
            "/lib64/dbus-1.0/include",
            "/lib/dbus-1.0/include"
        }) do
        for _, include_dir in ipairs(os.dirs(pattern)) do
            table.insert(include_dirs, include_dir)
        end
    end
    return include_dirs
end

function setup_platform_settings()
    if is_os("windows") then
        add_requires("libyuv", "miniaudio 0.11.21")
        add_defines("NOMINMAX")
        add_links("Shell32", "dwmapi", "User32", "kernel32",
            "gdi32", "winmm", "setupapi", "version",
            "Imm32", "iphlpapi", "d3d11", "dxgi")
        add_cxflags("/WX")
        set_runtimes("MT")
    elseif is_os("linux") then
        add_links("pulse-simple", "pulse")
        add_requires("libyuv")
        add_syslinks("pthread", "dl")
        add_links("asound", "X11", "Xext", "Xrender", "Xft", "Xtst",
            "Xrandr", "Xfixes")
        add_existing_include_dirs({
            "/usr/include/freetype2",
            "/usr/local/include/freetype2"
        }, {system = true})

        if is_config("USE_DRM", true) then
            add_links("drm")
            add_defines("CROSSDESK_HAS_DRM=1")
            add_existing_include_dirs({
                "/usr/include/libdrm",
                "/usr/local/include/libdrm"
            }, {system = true})
        else
            add_defines("CROSSDESK_HAS_DRM=0")
        end

        if is_config("USE_WAYLAND", true) then
            add_links("dbus-1")
            add_defines("CROSSDESK_HAS_WAYLAND_CAPTURER=1")
            add_existing_include_dirs({
                "/usr/include/dbus-1.0",
                "/usr/local/include/dbus-1.0",
                "/usr/include/pipewire-0.3",
                "/usr/local/include/pipewire-0.3",
                "/usr/include/pipewire",
                "/usr/local/include/pipewire",
                "/opt/crossdesk-pipewire-sdk/include/pipewire-0.3",
                "/usr/include/spa-0.2",
                "/usr/local/include/spa-0.2",
                "/usr/include/spa",
                "/usr/local/include/spa",
                "/opt/crossdesk-pipewire-sdk/include/spa-0.2"
            }, {system = true})
            for _, include_dir in ipairs(collect_dbus_arch_include_dirs()) do
                add_includedirs(include_dir, {system = true})
            end
        else
            add_defines("CROSSDESK_HAS_WAYLAND_CAPTURER=0")
        end

        add_cxflags("-Wno-unused-variable")
    elseif is_os("macosx") then
        if is_arch("x86_64") then
            add_ldflags("-Wl,-ld_classic")
        end
        add_cxflags("-Wno-unused-variable")
        add_frameworks("Cocoa", "OpenGL", "IOSurface", "ScreenCaptureKit",
            "AVFoundation", "CoreMedia", "CoreVideo", "CoreAudio",
            "AudioToolbox")
    end
end
