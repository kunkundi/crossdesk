set_project("projectx")
set_version("0.0.1")
set_license("LGPL-3.0")

add_rules("mode.release", "mode.debug")
set_languages("c++17")
set_encodings("utf-8")

set_installdir("$(projectdir)/out")

add_defines("ASIO_STANDALONE", "ASIO_HAS_STD_TYPE_TRAITS", "ASIO_HAS_STD_SHARED_PTR", 
    "ASIO_HAS_STD_ADDRESSOF", "ASIO_HAS_STD_ATOMIC", "ASIO_HAS_STD_CHRONO", 
    "ASIO_HAS_CSTDINT", "ASIO_HAS_STD_ARRAY",  "ASIO_HAS_STD_SYSTEM_ERROR",
    "NOMINMAX")

add_requires("asio 1.24.0", "nlohmann_json 3.11.3", "spdlog 1.14.1", "openfec 1.4.2", "libopus 1.5.1", "openh264 2.4.1", "dav1d 1.4.3", "libyuv 2024.5.21", "aom 3.9.0", {system = false}, {configs = {shared = false}})
add_packages("asio", "nlohmann_json", "spdlog", "openfec", "libopus", "openh264", "dav1d", "libyuv", "aom")

add_requires("vcpkg::libnice", {configs = {shared = false}})
add_packages("vcpkg::libnice")

includes("thirdparty")

if is_os("windows") then
    add_defines("_WEBSOCKETPP_CPP11_INTERNAL_")
    add_cxflags("/WX")
elseif is_os("linux") then
    add_requires("glib", {system = true})
    add_packages("glib")
    add_cxflags("-fPIC", "-Wno-unused-variable") 
    add_syslinks("pthread")
elseif is_os("macosx") then
    add_ldflags("-Wl,-ld_classic")
    add_cxflags("-Wno-unused-variable")
end

target("log")
    set_kind("object")
    add_files("src/log/log.cpp")
    add_includedirs("src/log", {public = true})

target("common")
    set_kind("object")
    add_deps("log")
    add_files("src/common/common.cpp", 
    "src/common/rtc_base/*.cc",
    "src/common/rtc_base/network/*.cc",
    "src/common/rtc_base/numerics/*.cc",
    "src/common/api/units/*.cc",
    "src/common/api/transport/*.cc")
    if not is_os("windows") then
        remove_files("src/common/rtc_base/win32.cc")
    end
    add_includedirs("src/common", {public = true})

target("inih")
    set_kind("object")
    add_files("src/inih/ini.c", "src/inih/INIReader.cpp")
    add_includedirs("src/inih", {public = true})

target("ringbuffer")
    set_kind("object")
    add_files("src/ringbuffer/ringbuffer.cpp")
    add_includedirs("src/ringbuffer", {public = true})

target("thread")
    set_kind("object")
    add_deps("log")
    add_files("src/thread/*.cpp")
    add_includedirs("src/thread", {public = true})

target("frame")
    set_kind("object")
    add_files("src/frame/*.cpp")
    add_includedirs("src/frame", {public = true})

target("fec")
    set_kind("object")
    add_deps("log")
    add_files("src/fec/*.cpp")
    add_includedirs("src/fec", {public = true})

target("statistics")
    set_kind("object")
    add_deps("log")
    add_files("src/statistics/*.cpp")
    add_includedirs("src/statistics", {public = true})

target("ice")
    set_kind("object")
    add_deps("log", "common", "ws")
    add_files("src/ice/*.cpp")
    add_includedirs("src/ws", "src/ice", {public = true})
    if is_os("windows") then
        add_includedirs(path.join(os.getenv("VCPKG_ROOT"), 
            "installed/x64-windows-static/include/glib-2.0"), {public = true})
        add_includedirs(path.join(os.getenv("VCPKG_ROOT"), 
            "installed/x64-windows-static/lib/glib-2.0/include"), {public = true})
    elseif is_os("macosx") then

    end
    
target("ws")
    set_kind("object")
    add_deps("log")
    add_files("src/ws/*.cpp")
    add_includedirs("src/ws", 
    "thirdparty/websocketpp/include", {public = true})

target("rtp")
    set_kind("object")
    add_deps("log", "common", "frame", "ringbuffer", "thread", "rtcp", "fec", "statistics")
    add_files("src/rtp/*.cpp", 
    "src/rtp/rtp_packet/*.cpp",
    "src/rtp/rtp_packetizer/*.cpp")
    add_includedirs("src/rtp", 
    "src/rtp/rtp_packet",
    "src/rtp/rtp_packetizer", {public = true})

target("rtcp")
    set_kind("object")
    add_deps("log", "common")
    add_files("src/rtcp/*.cpp",
    "src/rtcp/rtcp_packet/*.cpp",
    "src/rtcp/rtp_feedback/*.cpp")
    add_includedirs("src/rtcp",
    "src/rtcp/rtcp_packet",
    "src/rtcp/rtcp_sender",
    "src/rtcp/rtp_feedback", {public = true})

target("qos")
    set_kind("object")
    add_deps("log", "rtp", "rtcp")
    add_files("src/qos/*.cc", 
    "src/qos/*.cpp")
    add_includedirs("src/qos", {public = true})

target("channel")
    set_kind("object")
    add_deps("log", "rtp", "rtcp", "ice", "qos")
    add_files("src/channel/*.cpp", "src/channel/rtp_channel/*.cpp")
    add_includedirs("src/channel", "src/channel/rtp_channel", {public = true})

target("transport")
    set_kind("object")
    add_deps("log", "ws", "ice", "channel", "rtp", "rtcp", "statistics", "media")
    add_files("src/transport/*.cpp")
    add_includedirs("src/transport", {public = true})

target("media")
    set_kind("object")
    add_deps("log", "frame")
    if is_os("windows") then
        add_files("src/media/video/encode/*.cpp",
        "src/media/video/decode/*.cpp",
        "src/media/video/encode/nvcodec/*.cpp",
        "src/media/video/decode/nvcodec/*.cpp",
        "src/media/video/encode/openh264/*.cpp",
        "src/media/video/decode/openh264/*.cpp",
        "src/media/video/encode/aom/*.cpp",
        "src/media/video/decode/dav1d/*.cpp",
        "src/media/video/decode/aom/*.cpp",
        "src/media/nvcodec/*.cpp")
        add_includedirs("src/media/video/encode",
        "src/media/video/decode",
        "src/media/video/encode/nvcodec",
        "src/media/video/decode/nvcodec",
        "src/media/video/encode/openh264",
        "src/media/video/decode/openh264",
        "src/media/video/encode/aom",
        "src/media/video/decode/dav1d",
        "src/media/video/decode/aom",
        "src/media/nvcodec",
        "thirdparty/nvcodec/interface", {public = true})
        add_includedirs(path.join(os.getenv("CUDA_PATH"), "include"), {public = true})
    elseif is_os(("linux")) then
        add_files("src/media/video/encode/*.cpp",
        "src/media/video/decode/*.cpp",
        "src/media/video/encode/nvcodec/*.cpp",
        "src/media/video/decode/nvcodec/*.cpp",
        "src/media/video/encode/openh264/*.cpp",
        "src/media/video/decode/openh264/*.cpp",
        "src/media/video/encode/aom/*.cpp",
        "src/media/video/decode/dav1d/*.cpp",
        "src/media/video/decode/aom/*.cpp",
        "src/media/nvcodec/*.cpp")
        add_includedirs("src/media/video/encode",
        "src/media/video/decode",
        "src/media/video/encode/nvcodec",
        "src/media/video/decode/nvcodec",
        "src/media/video/encode/openh264",
        "src/media/video/decode/openh264",
        "src/media/video/encode/aom",
        "src/media/video/decode/dav1d",
        "src/media/video/decode/aom",
        "src/media/nvcodec",
        "thirdparty/nvcodec/interface", {public = true})
        add_includedirs(path.join(os.getenv("CUDA_PATH"), "include"), {public = true})
    elseif is_os("macosx") then
        add_files("src/media/video/encode/*.cpp",
        "src/media/video/decode/*.cpp",
        "src/media/video/encode/openh264/*.cpp",
        "src/media/video/decode/openh264/*.cpp",
        "src/media/video/encode/aom/*.cpp",
        "src/media/video/decode/dav1d/*.cpp",
        "src/media/video/decode/aom/*.cpp")
        add_includedirs("src/media/video/encode",
        "src/media/video/decode",
        "src/media/video/encode/openh264",
        "src/media/video/decode/openh264",
        "src/media/video/encode/aom",
        "src/media/video/decode/dav1d",
        "src/media/video/decode/aom", {public = true})
    end
    add_files("src/media/audio/encode/*.cpp",
        "src/media/audio/decode/*.cpp")
    add_includedirs("src/media/audio/encode",
        "src/media/audio/decode", "src/interface", {public = true})

target("pc")
    set_kind("object")
    add_deps("log", "ws", "ice", "transport", "inih", "common")
    add_files("src/pc/*.cpp")
    add_includedirs("src/pc", "src/interface", {public = true})

target("projectx")
    set_kind("static")
    add_deps("log", "pc")
    add_files("src/rtc/*.cpp")
    add_includedirs("src/rtc", "src/interface")

    if is_os("windows") then
        add_linkdirs("thirdparty/nvcodec/lib/x64")
        add_linkdirs(path.join(os.getenv("CUDA_PATH"), "lib/x64"))
        add_links("nice", "glib-2.0", "gio-2.0", "gmodule-2.0", "gobject-2.0",
        "pcre2-8", "pcre2-16", "pcre2-32", "pcre2-posix", 
        "zlib", "ffi", "libcrypto", "libssl", "intl", "iconv", 
        "Shell32", "Advapi32", "Dnsapi", "Shlwapi", "Crypt32", 
        "ws2_32", "Bcrypt", "windowsapp", "User32", "Strmiids", "Mfuuid",
        "Secur32", "Bcrypt")
        add_links("cuda", "nvencodeapi", "nvcuvid")
    elseif is_os(("linux")) then
        add_linkdirs("thirdparty/nvcodec/lib/x64")
        add_linkdirs(path.join(os.getenv("CUDA_PATH"), "lib/x64"))
        add_links("cuda", "nvidia-encode", "nvcuvid")
    elseif is_os("macosx") then

    end

    add_installfiles("src/interface/*.h", {prefixdir = "include"})
    -- add_rules("utils.symbols.export_list", {symbols = {
    --     "CreatePeer",
    --     "DestroyPeer",
    --     "Init",
    --     "CreateConnection",
    --     "JoinConnection",
    --     "LeaveConnection",
    --     "SendData"}})


    -- add_rules("utils.symbols.export_all", {export_classes = true})
-- after_install(function (target)
--     os.rm("$(projectdir)/out/lib/*.a")
--     os.rm("$(projectdir)/out/include/log.h")
-- end)

-- target("host")
--     set_kind("binary")
--     add_deps("projectx")
--     add_files("tests/peerconnection/host.cpp")
--     add_includedirs("src/interface")

-- target("guest")
--     set_kind("binary")
--     add_deps("projectx")
--     add_files("tests/peerconnection/guest.cpp")
--     add_includedirs("src/interface")

-- target("nicetest")
--     set_kind("binary")
--     add_files("tests/peerconnection/nice.cpp")
--     add_includedirs("E:/SourceCode/vcpkg/installed/x64-windows-static/include/glib-2.0")
--     add_includedirs("E:/SourceCode/vcpkg/installed/x64-windows-static/lib/glib-2.0/include")
--     add_linkdirs("E:/SourceCode/vcpkg/installed/x64-windows-static/lib")
--     add_links("nice", "glib-2.0", "gio-2.0", "gmodule-2.0", "gobject-2.0", "gthread-2.0",
--     "pcre2-8", "pcre2-16", "pcre2-32", "pcre2-posix", 
--     "zlib", "ffi", "libcrypto", "libssl", "intl", "iconv", "charset", "bz2",
--     "Shell32", "Advapi32", "Dnsapi", "Shlwapi", "Iphlpapi")

-- target("fec_client")
--     set_kind("binary")
--     add_packages("openfec")
--     add_files("tests/fec/simple_client.cpp")
--     add_includedirs("tests/fec")

-- target("fec_server")
--     set_kind("binary")
--     add_packages("openfec")
--     add_files("tests/fec/simple_server.cpp")
--     add_includedirs("tests/fec")

-- target("opus_test")
--     set_kind("binary")
--     add_packages("libopus", "sdl2")
--     add_files("tests/opus/OpusEncoderImpl.cpp",
--         "tests/opus/OpusDecoderImpl.cpp",
--         "tests/opus/main.cpp")
--     add_includedirs("tests/opus")