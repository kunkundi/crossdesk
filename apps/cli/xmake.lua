local source_dir = path.absolute("../..", os.scriptdir())
local controller_dir = path.join(source_dir, "libs/controller")
local terminal_dir = path.join(source_dir, "libs/terminal")
local minirtc_dir = path.join(source_dir, "deps/submodules/minirtc")

add_requires("spdlog 1.14.1", {system = false})
add_requires("asio 1.32.0", "websocketpp 0.8.2", {system = false})
add_requires("libpng v1.6.59", {system = false, configs = {shared = false}})
if is_plat("windows") then
    add_requires("zlib 1.3.1", {system = false, configs = {shared = false}})
end

target("crossdesk_controller")
    set_kind("static")
    add_deps("minirtc", "crossdesk_wire", "crossdesk_file_browser")
    add_packages("spdlog", "libpng", "libyuv")
    add_includedirs(path.join(controller_dir, "include"),
        path.join(minirtc_dir, "src/api"), {public = true})
    add_includedirs(path.join(minirtc_dir, "src/log"))
    add_files(path.join(controller_dir, "src/*.cpp"))
    remove_files(path.join(controller_dir, "src/file_browser.cpp"))
target_end()

target("crossdesk_terminal")
    set_kind("static")
    add_deps("crossdesk_controller")
    add_includedirs(path.join(terminal_dir, "include"), {public = true})
    add_files(path.join(terminal_dir, "src/*.cpp"))
    if is_plat("windows") then
        add_packages("zlib", {public = true})
    else
        add_syslinks("z", {public = true})
    end
target_end()

target("crossdesk-cli")
    set_kind("binary")
    set_version("0.2.0")
    add_deps("crossdesk_terminal")
    add_files("main.cpp")
    add_files("agent.cpp", "automation_protocol.cpp", "video_server.cpp")
    add_packages("asio", "websocketpp")
    add_files(is_plat("windows") and "terminal_io_windows.cpp" or "terminal_io_posix.cpp")
    if is_plat("windows") then
        add_syslinks("advapi32", "user32", "ws2_32")
        add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
    elseif is_plat("linux") then
        add_syslinks("rt")
    end
target_end()
