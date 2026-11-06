import("core.base.bytes")
import("core.project.config")
import("core.project.project")

function digest(value)
    -- Expand shared config tables so serialization never emits order-dependent refs.
    local data = table.clone(value, math.huge)
    return hash.sha256(bytes(string.serialize(data, {orderkeys = true, strip = true})))
end

function main(plat, arch, configfile)
    assert(plat and arch and configfile, "expected platform, architecture and config file")
    local outputfile = assert(os.getenv("GITHUB_OUTPUT"), "GITHUB_OUTPUT is required")
    local options = {
        plat = plat,
        arch = arch,
        mode = "release",
        CROSSDESK_BUILD_DESKTOP = false,
        CROSSDESK_BUILD_CLI = true,
        USE_CUDA = false,
        policies = "package.librarydeps.strict_compatibility"
    }
    if plat == "macosx" then
        options.target_minver = "13.0"
    end

    -- Use the same options for dependency discovery and the subsequent configure step.
    config.clear()
    for name, value in pairs(options) do
        config.set(name, value, {readonly = true, force = true})
    end
    project.check_options()
    local requires, requires_extra = project.requires_str()
    local requireconfs, requireconfs_extra = project.requireconfs_str()
    assert(requires and #requires > 0, "CLI dependency declarations are missing")

    -- These root settings are inherited by packages. Target source lists and
    -- application versions are deliberately outside the dependency identity.
    local package_policies = {}
    for name, value in pairs(project.get("target.policy") or {}) do
        if name:startswith("package.") then
            package_policies[name] = value
        end
    end
    local configuration = {
        options = options,
        runtimes = project.get("target.runtimes"),
        toolchains = project.get("target.toolchains"),
        lto = project.policy("build.optimization.lto"),
        asan = project.policy("build.sanitizer.address"),
        package_policies = package_policies
    }
    local dependencies = {
        requires = requires,
        requires_extra = requires_extra,
        requireconfs = requireconfs,
        requireconfs_extra = requireconfs_extra
    }
    io.save(configfile, options)
    local output = io.open(outputfile, "a")
    assert(output, "cannot open GitHub output file")
    output:print("configuration=%s", digest(configuration))
    output:print("dependencies=%s", digest(dependencies))
    output:close()
end
