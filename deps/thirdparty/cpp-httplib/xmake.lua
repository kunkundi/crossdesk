package("cpp-httplib")
    set_kind("library", {headeronly = true})
    set_homepage("https://github.com/yhirose/cpp-httplib")
    set_description("A C++11 single-file header-only cross platform HTTP/HTTPS library.")
    set_license("MIT")

    set_urls("https://github.com/yhirose/cpp-httplib/archive/refs/tags/$(version).tar.gz",
             "https://github.com/yhirose/cpp-httplib.git")

    add_versions("v0.26.0", "a66f908f50ccb119769adce44fe1eac75f81b6ffab7c4ac0211bb663ffeb2688")
    add_versions("v0.23.1", "410a1347ed6bcbcc4a19af8ed8ad3873fe9fa97731d52db845c4c78f3f9c31e6")
    add_versions("v0.22.0", "fcfea48c8f2c386e7085ef8545c8a4875efa30fa6d5cf9dd31f03c6ad038da9d")
    add_versions("v0.21.0", "99cfbce46981994d8f34ed21836510d7d408ffac91315bb22c9584a83e220e60")
    add_versions("v0.20.1", "b74b1c2c150be2841eba80192f64d93e9a6711985b3ae8aaa1a9cec4863d1dd9")
    add_versions("v0.20.0", "18064587e0cc6a0d5d56d619f4cbbcaba47aa5d84d86013abbd45d95c6653866")
    add_versions("v0.19.0", "c9b9e0524666e1cd088f0874c57c1ce7c0eaa8552f9f4e15c755d5201fc8c608")
    add_versions("v0.18.7", "b7b1e9e4e77565a5a9bc95e761d5df3e7c0e8ca37c90fd78b1b031bc6cb90fc1")
    add_versions("v0.18.6", "8900747bba3dda8007f1876175be699036e09e4a25ceeab51196d9365bf1993a")
    add_versions("v0.18.5", "731190e97acd63edce57cc3dacd496f57e7743bfc7933da7137cb3e93ec6c9a0")
    add_versions("v0.18.3", "a0567bcd6c3fe5cef1b329b96245119047f876b49e06cc129a36a7a8dffe173e")
    add_versions("v0.18.1", "405abd8170f2a446fc8612ac635d0db5947c0d2e156e32603403a4496255ff00")
    add_versions("v0.17.3", "95bd6dba4241656c59d6f0854d408d14c220f7c71e673319ee27d30aee741aaa")
    add_versions("v0.17.0", "dd3ba355b6aa74b7a0bff982ad0de7af6d9672fd83af30aa84eb707974d2a903")
    add_versions("v0.16.3", "c1742fc7179aaae2a67ad9bba0740b7e9ffaf4f5e62feef53101ecdef1478716")
    add_versions("v0.16.2", "75565bcdf12522929a26fb57a2c7f8cc0e175e27a9ecf51616075f3ea960da44")
    add_versions("v0.15.3", "2121bbf38871bb2aafb5f7f2b9b94705366170909f434428352187cb0216124e")
    add_versions("v0.15.2", "4afbcf4203249d2cbcb698e46e1f6fb61b479013a84844d6bb1c044e233cab6a")
    add_versions("v0.15.1", "8d6a4a40ee8fd3f553b7e895882e60e674bd910883fc1857587dbbabee3cdb91")
    add_versions("v0.15.0", "b658e625e283e2c81437a485a95f3acf8b1d32c53d8147b1ccecc8f630e1f7bb")
    add_versions("v0.14.3", "dcf6486d9030937636d8a4f820ca9531808fd7edb283893dddbaa05f99357e63")
    add_versions("v0.14.2", "dbcf5590e8ed35c6745c2ad659a5ebec92f05187d1506eec24449d6db95e5084")
    add_versions("v0.14.1", "2d4fb5544da643e5d0a82585555d8b7502b4137eb321a4abbb075e21d2f00e96")
    add_versions("v0.14.0", "3a92248ef8cf2c32ad07f910b8e3052ff2427022b2adb871cf326fb620d2438e")
    add_versions("v0.12.6", "24bc594a9efcc08a5a6f3928e848d046d411a88b07bcd6f7f3851227a1f0133e")
    add_versions("v0.12.1", "0e56c25c63e730ebd42e2beda6e7cb1b950131d8fc00d3158b1443a8d76f41ca")
    add_versions("v0.9.2", "bfef2587a2aa31c85fb361df71c720be97076f8083e4f3881da8572f6a58054f")
    add_versions("v0.8.5", "b353f3e7c124a08940d9425aeb7206183fa29857a8f720c162f8fd820cc18f0e")

    add_patches("v0.26.0", path.join(os.scriptdir(), "patches/v0.26.0/fix-mingw.diff"), "f7b704e86abd8fd04217056e3ffb01427185e0bae72999246a3b8d13ba23c56a")
    add_patches("v0.23.1", path.join(os.scriptdir(), "patches/v0.23.1/fix-mingw.diff"), "d2d8a4c16de3a00d9872526a187257c7ad344eba2a9f109d10b58eadce1c4059")

    add_configs("ssl",  { description = "Requires OpenSSL", default = false, type = "boolean"})
    add_configs("zlib",  { description = "Requires Zlib", default = false, type = "boolean"})
    add_configs("brotli",  { description = "Requires Brotli", default = false, type = "boolean"})
    add_configs("exceptions", {description = "Enable the use of C++ exceptions", default = true, type = "boolean"})

    add_deps("cmake")

    if on_check then
        on_check(function (package)
            local pkg_ver = package:version()
            if package:is_plat("windows") then
                local winver = winos.version()
                local vs = package:toolchain("msvc"):config("vs")
                assert(winver and winver:gt("win8"), "package(httplib): Windows 8 or lower is neither supported nor tested.")
                if pkg_ver and pkg_ver:ge("0.19.0") then
                    assert(vs and tonumber(vs) > 2015, "package(httplib >= 0.19.0): VS 2015 or lower is neither supported nor tested.")
                elseif pkg_ver and pkg_ver:ge("0.11.0") then
                    assert(vs and tonumber(vs) > 2013, "package(httplib >= 0.11.0): VS 2013 or lower is neither supported nor tested.")
                end
            end
            if pkg_ver and pkg_ver:ge("0.15.0") and package:is_plat("msys") then
                wprint("package(httplib): MSYS2 (including MinGW) is not officially supported or tested by httplib.")
            end
            if package:is_plat("android") then
                local ndk = package:toolchain("ndk")
                local ndk_sdkver = ndk:config("ndk_sdkver")
                assert(ndk_sdkver and tonumber(ndk_sdkver) >= 24, "package(httplib): need ndk api level >= 24 for android")
            end
            if pkg_ver and pkg_ver:ge("0.23.0") then
                if package:check_sizeof("void*") == "4" then
                    raise("package(cpp-httplib >=0.23.0) does not support 32-bit")
                end
            end
        end)
    end

    on_load(function (package)
        if package:config("ssl") then
            package:add("deps", "openssl3 3.3.2")
            package:add("defines", "CPPHTTPLIB_OPENSSL_SUPPORT")
        end
        if package:config("zlib") then
            package:add("deps", "zlib")
            package:add("defines", "CPPHTTPLIB_ZLIB_SUPPORT")
        end
        if package:config("brotli") then
            package:add("deps", "brotli")
            package:add("defines", "CPPHTTPLIB_BROTLI_SUPPORT")
        end
    end)

    on_install("!cygwin", function (package)
        local configs = {"-DHTTPLIB_COMPILE=OFF"}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "Release"))
        table.insert(configs, "-DBUILD_SHARED_LIBS=" .. (package:config("shared") and "ON" or "OFF"))
        table.insert(configs, "-DHTTPLIB_REQUIRE_OPENSSL=" .. (package:config("ssl") and "ON" or "OFF"))
        table.insert(configs, "-DHTTPLIB_REQUIRE_ZLIB=" .. (package:config("zlib") and "ON" or "OFF"))
        table.insert(configs, "-DHTTPLIB_REQUIRE_BROTLI=" .. (package:config("brotli") and "ON" or "OFF"))
        table.insert(configs, "-DHTTPLIB_NO_EXCEPTIONS=" .. (package:config("exceptions") and "OFF" or "ON"))

        if package:config("ssl") then
            local openssl = package:dep("openssl" .. (package:version():ge("0.15.0") and "3" or ""))
            if not openssl:is_system() then
                table.insert(configs, "-DOPENSSL_ROOT_DIR=" .. openssl:installdir())
            end
        end
        import("package.tools.cmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            void test() {
                httplib::Client cli("http://cpp-httplib-server.yhirose.repl.co");
            }
        ]]}, {configs = {languages = "c++11"}, includes = "httplib.h"}))
    end)