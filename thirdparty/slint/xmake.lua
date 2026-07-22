package("slint")
    set_homepage("https://slint.dev")
    set_description("Declarative GUI toolkit for C++")
    set_license("GPL-3.0-only OR LicenseRef-Slint-Royalty-free-2.0 OR LicenseRef-Slint-Software-3.0")

    add_urls("https://github.com/slint-ui/slint/archive/refs/tags/v$(version).tar.gz",
             "https://github.com/slint-ui/slint.git")
    add_versions("1.17.1", "68222567f8c70ff677cd4a98cd94fb4765ac0f797eb8f8608a646911c908dc2a")

    add_configs("shared", {description = "Build the Slint runtime as a shared library", default = true, type = "boolean", readonly = true})

    add_deps("cmake")
    add_deps("rust 1.92.0", {host = true, private = true, system = false})

    on_load(function(package)
        package:add("includedirs", "include/slint")
        if package:is_plat("windows") then
            package:add("links", "slint_cpp.dll")
        else
            package:add("links", "slint_cpp")
        end
    end)

    on_install("windows", "linux", "macosx", function(package)
        local configs = {
            "-DSLINT_BUILD_TESTING=OFF",
            "-DSLINT_BUILD_EXAMPLES=OFF",
            "-DSLINT_FEATURE_INTERPRETER=OFF",
            "-DSLINT_FEATURE_LIVE_PREVIEW=OFF",
            "-DSLINT_FEATURE_TESTING=OFF",
            "-DSLINT_FEATURE_SYSTEM_TESTING=OFF",
            "-DSLINT_FEATURE_MCP=OFF",
            "-DSLINT_FEATURE_BACKEND_QT=OFF",
            "-DSLINT_FEATURE_RENDERER_SKIA=OFF",
            "-DSLINT_FEATURE_RENDERER_SKIA_OPENGL=OFF",
            "-DSLINT_FEATURE_RENDERER_SKIA_VULKAN=OFF",
            "-DSLINT_FEATURE_RENDERER_FEMTOVG=ON",
            "-DSLINT_FEATURE_RENDERER_SOFTWARE=ON",
            "-DSLINT_STYLE=fluent",
            "-DBUILD_SHARED_LIBS=ON"
        }
        import("package.tools.cmake").install(package, configs)
    end)

    on_test(function(package)
        assert(package:has_cxxincludes("slint.h", {configs = {languages = "c++20"}}), "Slint C++ headers are unusable")
        assert(os.isfile(path.join(package:installdir(), "bin", is_host("windows") and "slint-compiler.exe" or "slint-compiler")), "slint-compiler was not installed")
    end)
package_end()
