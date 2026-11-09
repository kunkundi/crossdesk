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
    -- Discard unreferenced functions/data without the extra build cost of LTO.
    -- Keep debug builds unchanged for predictable debugger behavior.
    if is_mode("release") then
        if is_os("macosx") then
            add_ldflags("-Wl,-dead_strip")
        elseif is_os("linux") then
            add_cxflags("-ffunction-sections", "-fdata-sections")
            add_ldflags("-Wl,--gc-sections")
        elseif is_os("windows") then
            add_cxflags("/Gy", "/Gw")
            add_ldflags("/OPT:REF", "/OPT:ICF")
        end
    end

    if is_os("windows") then
        add_requires("libyuv", "miniaudio 0.11.21")
        add_defines("NOMINMAX")
        add_links("Shell32", "dwmapi", "User32", "kernel32",
            "gdi32", "winmm", "setupapi", "version",
            "Imm32", "iphlpapi", "d3d11", "dxgi")
        add_cxflags("/WX")
        set_runtimes("MT")
    elseif is_os("linux") then
        -- MiniRTC carries its own static GLib/GIO. System GTK and AT-SPI must
        -- keep using the system runtime, including its separate GType registry.
        -- Partial ELF interposition otherwise registers GCancellable twice and
        -- leaves ICE stream initialization stuck in g_once_init_enter().
        add_ldflags("-Wl,--exclude-libs,libgio-2.0.a:libgobject-2.0.a:libgthread-2.0.a:libgmodule-2.0.a:libglib-2.0.a:libffi.a",
            {force = true})
        add_requires("pkgconfig::atspi-2", {alias = "atspi", system = true, optional = true})
        add_links("pulse-simple", "pulse")
        add_requires("libyuv")
        add_syslinks("pthread", "dl")
        add_links("asound", "X11", "Xext", "Xrender", "Xft", "Xtst",
            "Xrandr", "Xfixes", "Xcursor")
        add_existing_include_dirs({
            "/usr/include/freetype2",
            "/usr/local/include/freetype2"
        }, {system = true})

        -- Screen sharing power inhibitors also use D-Bus in X11-only builds.
        add_links("dbus-1")
        add_existing_include_dirs({
            "/usr/include/dbus-1.0",
            "/usr/local/include/dbus-1.0"
        }, {system = true})
        for _, include_dir in ipairs(collect_dbus_arch_include_dirs()) do
            add_includedirs(include_dir, {system = true})
        end

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
            add_defines("CROSSDESK_HAS_WAYLAND_CAPTURER=1")
            add_existing_include_dirs({
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
        else
            add_defines("CROSSDESK_HAS_WAYLAND_CAPTURER=0")
        end

        add_cxflags("-Wno-unused-variable")
    elseif is_os("macosx") then
        add_cxflags("-Wno-unused-variable")
        add_frameworks("Cocoa", "CoreGraphics", "Metal", "QuartzCore", "IOKit",
            "IOSurface",
            "ScreenCaptureKit", "AVFoundation", "CoreMedia", "CoreVideo",
            "CoreAudio", "AudioToolbox")
    end
end
