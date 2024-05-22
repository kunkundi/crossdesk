package("aom")

    set_homepage("https://aomedia.googlesource.com/aom/")
    set_description("AV1 Codec Library")
    set_license("BSD-3-Clause")
    set_urls("https://aomedia.googlesource.com/aom.git")
    add_versions("v3.9.0", "6cab58c3925e0f4138e15a4ed510161ea83b6db1")

    add_deps("cmake")

    if is_os("windows") then
        add_defines("_CRT_SECURE_NO_WARNINGS")
    end

    on_install("windows", "linux", "macosx", function (package)
        local configs = {"-DENABLE_EXAMPLES=OFF", "-DENABLE_TESTS=OFF", "-DENABLE_TOOLS=OFF", "-DENABLE_DOCS=OFF"}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))
        import("package.tools.cmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:has_cfuncs("aom_codec_version", {includes = "aom/aom_codec.h"}))
    end)