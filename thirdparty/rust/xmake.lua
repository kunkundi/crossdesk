package("rust")
    set_kind("toolchain")
    set_homepage("https://rust-lang.org")
    set_description("Rust is a general-purpose programming language emphasizing performance, type safety, and concurrency.")

    add_versions("1.92.0", "")

    add_deps("ca-certificates", {host = true, private = true})
    add_deps("rustup", {host = true, private = true, system = false})

    on_install(function(package)
        import("core.tools.rustc.target_triple")

        local host_target = assert(target_triple(package:plat(), package:arch()),
            "failed to determine the Rust host target triple")
        local version = package:version():shortstr()
        local toolchain_name = version .. "-" .. host_target
        local rustup_home = assert(os.getenv("RUSTUP_HOME"), "cannot find rustup home")

        os.vrunv("rustup", {"install", "--no-self-update", toolchain_name})
        os.vmv(path.join(rustup_home, "toolchains", toolchain_name, "*"), package:installdir())

        -- Keep the Xmake-managed toolchain isolated from rustup after installation.
        os.vrm(path.join(rustup_home, "toolchains", toolchain_name))
        os.vrm(path.join(rustup_home, "update-hashes", toolchain_name))

        package:addenv("RC", "bin/rustc" .. (is_host("windows") and ".exe" or ""))
        package:mark_as_pathenv("RC")
    end)

    on_test(function(package)
        os.vrun("cargo --version")
        os.vrun("rustc --version")
    end)
package_end()
