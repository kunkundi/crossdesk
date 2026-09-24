rule("slint")
    set_extensions(".slint")

    after_load(function(target)
        local outputdir = path.join(target:autogendir(), "rules", "slint")
        target:add("includedirs", outputdir, {public = true})

        local sourcebatch = target:sourcebatches()["slint"]
        local outputs = {}
        for _, sourcefile in ipairs(sourcebatch and sourcebatch.sourcefiles or {}) do
            local header = path.join(outputdir, path.basename(sourcefile) .. ".h")
            local sources = {}
            -- Slint supports moving definitions out of the header and splitting
            -- them into independently compiled translation units.
            for index = 1, 4 do
                local cppfile = path.join(outputdir,
                    path.basename(sourcefile) .. "_" .. index .. ".cpp")
                table.insert(sources, cppfile)
                target:add("files", cppfile, {always_added = true})
            end
            outputs[sourcefile] = {header = header, sources = sources}
        end
        target:data_set("slint.outputs", outputs)
    end)

    on_config(function(target)
        if target:rule("c++.build") then
            local cpp_rule = target:rule("c++.build"):clone()
            cpp_rule:add("deps", "slint", {order = true})
            target:rule_add(cpp_rule)
        end
    end)

    before_build_file(function(target, sourcefile, opt)
        import("core.project.depend")
        import("core.tools.gcc.parse_deps", {alias = "parse_deps"})
        import("utils.progress")

        local package = assert(target:pkg("slint"), "the slint package is required by this target")
        local compiler = path.join(package:installdir(), "bin",
            is_host("windows") and "slint-compiler.exe" or "slint-compiler")
        local outputs = target:data("slint.outputs")[sourcefile]
        local depfile = outputs.header .. ".d"
        -- Slint writes the -o path literally into each generated #include.
        local argv = {sourcefile, "-f", "cpp", "-o", path.absolute(outputs.header),
            "--depfile", depfile, "--style", "fluent",
            "--embed-resources=embed-files", "--cpp-namespace", "crossdesk::ui"}
        local missing_output = not os.isfile(outputs.header)
        for _, cppfile in ipairs(outputs.sources) do
            table.insert(argv, "--cpp-file")
            table.insert(argv, path.absolute(cppfile))
            missing_output = missing_output or not os.isfile(cppfile)
        end

        local function dependencies()
            local files = {sourcefile, compiler}
            if os.isfile(depfile) then
                -- Track imported components AND embedded images, including
                -- resources outside the UI directory, using the compiler output.
                table.join2(files, parse_deps(io.readfile(depfile, {continuation = "\\"})))
            end
            files = table.unique(files)
            table.sort(files)
            return files
        end

        local depopts = {dependfile = target:dependfile(outputs.header),
            values = argv, files = dependencies(),
            changed = target:is_rebuilt() or missing_output or not os.isfile(depfile)}
        -- Slint preserves timestamps of unchanged outputs. Use the dependency
        -- cache's successful-generation time, not the oldest output's time.
        depend.on_changed(function()
            progress.show(opt.progress, "${color.build.object}generating.slint %s", sourcefile)
            os.mkdir(path.directory(outputs.header))
            os.vrunv(compiler, argv)
            -- Save the freshly discovered dependencies on the first build too.
            depopts.files = dependencies()
        end, depopts)
    end)
rule_end()
