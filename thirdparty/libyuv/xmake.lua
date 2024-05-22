package("libyuv")

    set_homepage("https://chromium.googlesource.com/libyuv/libyuv/")
    set_description("libyuv is an open source project that includes YUV scaling and conversion functionality.")
    set_license("BSD-3-Clause")
    set_urls("https://chromium.googlesource.com/libyuv/libyuv.git")
    add_versions("2024.5.21", "8e18fc93c8c07d2ba6f9671281d6f35c8c47b2f4")

    add_deps("cmake")

    on_install("windows", "linux", "macosx", "android", "cross", "bsd", "mingw", function (package)
        local configs = {"-DTEST=OFF"}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))
        
        io.replace("CMakeLists.txt", "INSTALL ( PROGRAMS ${CMAKE_BINARY_DIR}/yuvconvert DESTINATION bin )", "", {plain = true})
        import("package.tools.cmake").install(package, configs)
        
        if package:is_plat("macosx", "linux", "android") then
            if package:config("shared") then 
                os.tryrm(package:installdir("lib", "*.a"))
            else 
                os.tryrm(package:installdir("lib", "*.so"))
            end
        end
    end)

    on_test(function (package)
        assert(package:has_cfuncs("I420Rotate", {includes = "libyuv/rotate.h"}))
    end)