-- Embed notices so the About pages also work in offline/portable installations.
rule("crossdesk.about_resources")
    after_load(function(target)
        target:add("includedirs", path.join(target:autogendir(), "about"))
    end)

    before_build(function(target)
        import("core.base.json")
        import("core.project.config")

        local root = os.projectdir()
        local function read(relative)
            return assert(io.readfile(path.join(root, relative)))
        end
        local function git(directory, args)
            local result
            try {function() result = os.iorunv("git", table.join({"-C", directory}, args)) end}
            return result and result:trim() or ""
        end
        local repository = "https://github.com/kunkundi/crossdesk"
        local revision = git(root, {"rev-parse", "HEAD"})
        local source = repository .. (revision ~= "" and "/tree/" .. revision or "")
        local mini_root = path.join(root, "deps/submodules/minirtc")
        local mini_revision = git(mini_root, {"rev-parse", "HEAD"})
        local mini_source = "https://github.com/kunkundi/minirtc" ..
            (mini_revision ~= "" and "/tree/" .. mini_revision or "")
        local catalog = json.decode(read("apps/ios/CrossDeskMobile/Resources/ThirdPartyLicenses.json"))
        local known = {}
        for _, component in ipairs(catalog.components) do known[component.id] = component end

        -- These desktop dependencies are not part of the shared mobile catalog.
        local desktop = {
            slint = {"Slint", "https://github.com/slint-ui/slint/tree/v"},
            libsdl3 = {"SDL", "https://github.com/libsdl-org/SDL/tree/release-"},
            tinyfiledialogs = {"tinyfiledialogs", "https://sourceforge.net/projects/tinyfiledialogs/"},
            ["cpp-httplib"] = {"cpp-httplib", "https://github.com/yhirose/cpp-httplib/tree/v"},
            openh264 = {"OpenH264", "https://github.com/cisco/openh264/tree/v"},
            miniaudio = {"miniaudio", "https://github.com/mackron/miniaudio/tree/"},
            aom = {"libaom", "https://aomedia.googlesource.com/aom/+/v"}
        }
        local components = {{
            id = "crossdesk", name = "CrossDesk", version = config.get("CROSSDESK_VERSION") or "0.0.0",
            license = "GPL-3.0-only", sourceURL = source,
            documents = {{title = "GNU GPL v3", text = read("LICENSE")}}
        }, {
            id = "minirtc", name = "MiniRTC", version = mini_revision:sub(1, 12),
            license = "LGPL-3.0-only", sourceURL = mini_source,
            documents = {{title = "GNU LGPL v3", text = read("deps/submodules/minirtc/LICENSE")},
                         {title = "GNU GPL v3", text = read("LICENSE")}}
        }}
        if target:is_plat("macosx") then
            table.insert(components, {
                id = "snug", name = "Snug (virtual display)", version = "2.8.0",
                license = "MIT", sourceURL = "https://github.com/zephyr4123/snug/tree/v2.8.0",
                documents = {{title = "MIT", text = read("apps/desktop/resources/macos/Snug-LICENSE.txt")}}
            })
        end
        local seen = {}
        for _, dependency in ipairs(table.join({target}, target:orderdeps())) do
            for _, package in ipairs(dependency:orderpkgs()) do
                local name = package:name()
                if not seen[name] then
                    seen[name] = true
                    local version = package:get("version") or ""
                    local shared = known[name]
                    local extra = desktop[name]
                    local item = {id = name, name = shared and shared.name or extra and extra[1] or name,
                        version = version, license = package:license() or "",
                        sourceURL = "https://github.com/xmake-io/xmake-repo/tree/master/packages/" ..
                            name:sub(1, 1) .. "/" .. name,
                        documents = json.mark_as_array({})}
                    if shared and shared.version:gsub("^v", "") == version:gsub("^v", "") then
                        item.license = shared.license
                        item.sourceURL = shared.sourceURL
                        item.documents = shared.documents
                    elseif extra then
                        item.sourceURL = extra[2] .. (name == "tinyfiledialogs" and "" or version:gsub("^v", ""))
                    end
                    -- Prefer installed notices when a package is absent from the shared catalog.
                    if #item.documents == 0 then
                        local install = package:installdir()
                        if install then
                            for _, file in ipairs(os.files(path.join(install, "share/licenses/**"))) do
                                local text = io.readfile(file)
                                if text and not text:find("\0", 1, true) then
                                    table.insert(item.documents, {title = path.relative(file, install), text = text})
                                end
                            end
                            local notice_header = name == "tinyfiledialogs" and "include/tinyfiledialogs.h"
                                or name == "openh264" and "include/wels/codec_api.h"
                            if notice_header then
                                local header_path = path.join(install, notice_header)
                                local header = os.isfile(header_path) and io.readfile(header_path)
                                local notice = header and header:match("^/%*.-%*/")
                                if notice then
                                    table.insert(item.documents, {title = notice_header, text = notice})
                                end
                            end
                        end
                    end
                    if name == "slint" then
                        table.insert(item.documents, {title = "GNU GPL v3", text = read("LICENSE")})
                    end
                    table.insert(components, item)
                end
            end
        end
        table.sort(components, function(a, b)
            if a.id == b.id then return false end
            if a.id == "crossdesk" then return true end
            if b.id == "crossdesk" then return false end
            return a.name:lower() < b.name:lower()
        end)
        local data = json.encode({components = components, revision = revision:sub(1, 12),
            releaseNotes = json.decode(read("apps/desktop/resources/release-notes.json")),
            sourceURL = source,
            buildURL = repository .. "/blob/" .. (revision ~= "" and revision or "main") .. "/docs/BUILD_EN.md"},
            {pure = true, orderkeys = true})
        -- A byte array avoids MSVC's limit on individual/concatenated string literals.
        local bytes = {}
        for index = 1, #data do
            bytes[index] = tostring(data:byte(index)) .. (index % 32 == 0 and "\n" or "")
        end
        local output = [[/*
 * @Author: DI JUNKUN
 * @Date: 2026-10-21
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _ABOUT_RESOURCES_H_
#define _ABOUT_RESOURCES_H_

// Generated by the desktop build. Do not edit.
inline constexpr unsigned char kAboutResources[] = {
]] .. table.concat(bytes, ",") .. "\n};\n\n#endif\n"
        local destination = path.join(target:autogendir(), "about", "about_resources.h")
        if not os.isfile(destination) or io.readfile(destination) ~= output then
            os.mkdir(path.directory(destination))
            io.writefile(destination, output)
        end
    end)
rule_end()
