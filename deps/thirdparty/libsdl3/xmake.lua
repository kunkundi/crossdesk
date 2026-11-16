package("crossdesk_sdl3")
    set_base("libsdl3")

    -- SDL 3.2.26 retains invalid XIM/XIC handles when the input method exits.
    -- Keep the fix in a separate package so cached unpatched SDL is not reused.
    add_patches("3.2.26", path.join(os.scriptdir(), "patches/3.2.26-x11-ime-lifetime.patch"),
        "6b5b5433630d58f5911f03506eefa60fb34a49d2eec51aaa478c0cf6748b303d")
package_end()
