-- All mobile controllers use the same authenticated outgoing session as desktop.
function add_mobile_direct_connection(source_dir)
    local lan_dir = path.join(source_dir, "apps/desktop/src/network/lan")
    local common_dir = path.join(source_dir, "apps/desktop/src/common")
    add_includedirs(lan_dir, common_dir, path.join(source_dir, "deps/submodules/minirtc/src/api"))
    add_packages("asio", "websocketpp", "openssl3", "nlohmann_json")
    add_defines("ASIO_STANDALONE")
    add_files(path.join(lan_dir, "*.cpp"), path.join(common_dir, "private_file.cpp"),
        path.join(source_dir, "libs/controller/src/mobile_direct_connection.cpp"))
end
