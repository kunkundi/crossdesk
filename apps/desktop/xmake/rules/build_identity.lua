-- Record the sources used by this build, including local QoS fixes. No Git
-- command runs on the user's machine when logs or diagnostics are produced.
rule("crossdesk.build_identity")
    after_load(function(target)
        target:add("includedirs", path.join(target:autogendir(), "build_identity"))
    end)

    before_build(function(target)
        local root = os.getenv("CROSSDESK_SOURCE_DIR") or os.projectdir()
        local function identity(directory)
            -- A source archive nested in an unrelated checkout must not inherit
            -- that checkout's revision. Submodule/worktree .git files are valid.
            local metadata = path.join(directory, ".git")
            if not os.isdir(metadata) and not os.isfile(metadata) then
                return "unknown", "unknown"
            end
            local revision, status
            try {function()
                revision = os.iorunv("git", {"-C", directory, "rev-parse", "HEAD"}):trim()
                status = os.iorunv("git", {"-C", directory, "status", "--porcelain",
                    "--untracked-files=normal", "--ignore-submodules=none"}):trim()
            end}
            if not revision or not revision:match("^[0-9a-fA-F]+$") then
                return "unknown", "unknown"
            end
            return revision, status and (status == "" and "clean" or "dirty") or "unknown"
        end
        local revision, state = identity(root)
        local mini_revision, mini_state = identity(path.join(root, "deps/submodules/minirtc"))
        local output = [[/*
 * @Author: DI JUNKUN
 * @Date: 2026-11-15
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _BUILD_IDENTITY_GENERATED_H_
#define _BUILD_IDENTITY_GENERATED_H_

// Generated at build time. Do not edit.
]] .. '#define CROSSDESK_BUILD_REVISION "' .. revision .. '"\n' ..
            '#define CROSSDESK_BUILD_STATE "' .. state .. '"\n' ..
            '#define MINIRTC_BUILD_REVISION "' .. mini_revision .. '"\n' ..
            '#define MINIRTC_BUILD_STATE "' .. mini_state .. '"\n\n#endif\n'
        local destination = path.join(target:autogendir(), "build_identity", "build_identity_generated.h")
        if not os.isfile(destination) or io.readfile(destination) ~= output then
            os.mkdir(path.directory(destination))
            io.writefile(destination, output)
            -- Git state can change twice within the build system's timestamp
            -- resolution. Explicitly rebuild this small object and invalidate
            -- only its consumers' link records so no stale identity survives.
            target:data_set("rebuilt", true)
            import("core.project.project")
            for _, consumer in pairs(project.targets()) do
                if consumer:kind() ~= "object" and consumer:kind() ~= "phony" then
                    for _, dependency in ipairs(consumer:orderdeps()) do
                        if dependency:name() == target:name() then
                            os.tryrm(consumer:dependfile())
                            break
                        end
                    end
                end
            end
        end
    end)
rule_end()
