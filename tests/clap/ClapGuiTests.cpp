/*
    This file is part of the OSCQuery Automation Plugin —
    https://github.com/pob31/OSCQuery-Automation-plugin

    Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle

    The OSCQuery Automation Plugin is free software: you can redistribute it
    and/or modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the License,
    or (at your option) any later version. It is distributed in the hope that it
    will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
    Public License (LICENSE, at the repository root) for more details.

    SPDX-License-Identifier: GPL-3.0-or-later
*/

/*
    THE GUI EXTENSION, 5b-4: the platform's embedded api is the one offered;
    the editor is created, sized by its hints, clamped by adjust_size, resized
    by set_size, destroyed; on Windows, parented to a window of the test's,
    it is a child of that window with the size it was told, in the window's
    own pixels. Parenting on macOS and X11 is the bench's (the test
    executable has no window toolkit of its own there).
*/

#include <doctest/doctest.h>

#include "TestHost.h"
#include "TestSupport.h"

#include <string>

using namespace oqatest::claphost;

namespace
{
    constexpr const char* eightSlots = "io.github.pob31.oqa.Q008";

    const char* platformApi()
    {
      #if defined (_WIN32)
        return CLAP_WINDOW_API_WIN32;
      #elif defined (__APPLE__)
        return CLAP_WINDOW_API_COCOA;
      #else
        return CLAP_WINDOW_API_X11;
      #endif
    }

    struct Ready
    {
        RecordingHost host;
        PluginHandle plugin;
        const clap_plugin_gui_t* gui = nullptr;

        explicit Ready (const char* id = eightSlots) : plugin (host, id)
        {
            REQUIRE (plugin);
            REQUIRE (plugin->init (plugin.get()));
            gui = plugin.extension<clap_plugin_gui_t> (CLAP_EXT_GUI);
            REQUIRE (gui != nullptr);
        }

        std::pair<uint32_t, uint32_t> size() const
        {
            uint32_t w = 0, h = 0;
            REQUIRE (gui->get_size (plugin.get(), &w, &h));
            return { w, h };
        }
    };
}

//==============================================================================
TEST_CASE ("clap gui: the platform's embedded window api is supported and preferred; floating windows are not")
{
    Ready r;
    CHECK (r.gui->is_api_supported (r.plugin.get(), platformApi(), false));
    CHECK_FALSE (r.gui->is_api_supported (r.plugin.get(), platformApi(), true));
    CHECK_FALSE (r.gui->is_api_supported (r.plugin.get(), "wayland", false));
    CHECK_FALSE (r.gui->is_api_supported (r.plugin.get(), CLAP_WINDOW_API_UIKIT, false));

    const char* api = nullptr;
    bool floating = true;
    REQUIRE (r.gui->get_preferred_api (r.plugin.get(), &api, &floating));
    CHECK (std::string (api) == platformApi());
    CHECK_FALSE (floating);
}

TEST_CASE ("clap gui: created, the editor opens at its default size, is resizable within its limits, takes a size, and is destroyed")
{
    Ready r;
    REQUIRE (r.gui->create (r.plugin.get(), platformApi(), false));

    /*  No parent yet: one pixel is one pixel. */
    CHECK (r.size() == std::make_pair (1100u, 620u));
    CHECK (r.gui->can_resize (r.plugin.get()));

    clap_gui_resize_hints_t hints {};
    REQUIRE (r.gui->get_resize_hints (r.plugin.get(), &hints));
    CHECK (hints.can_resize_horizontally);
    CHECK (hints.can_resize_vertically);
    CHECK_FALSE (hints.preserve_aspect_ratio);

    uint32_t w = 100, h = 100;
    REQUIRE (r.gui->adjust_size (r.plugin.get(), &w, &h));
    CHECK (w == 720);
    CHECK (h == 420);

    w = 5000; h = 5000;
    REQUIRE (r.gui->adjust_size (r.plugin.get(), &w, &h));
    CHECK (w == 2400);
    CHECK (h == 1600);

    w = 900; h = 500;
    REQUIRE (r.gui->adjust_size (r.plugin.get(), &w, &h));
    CHECK (w == 900);
    CHECK (h == 500);
    REQUIRE (r.gui->set_size (r.plugin.get(), 900, 500));
    CHECK (r.size() == std::make_pair (900u, 500u));

    CHECK_FALSE (r.gui->set_scale (r.plugin.get(), 1.5));

    r.gui->destroy (r.plugin.get());

    /*  Created again after a destroy: fresh, at the default size. */
    REQUIRE (r.gui->create (r.plugin.get(), platformApi(), false));
    CHECK (r.size() == std::make_pair (1100u, 620u));
    r.gui->destroy (r.plugin.get());
}

#if defined (_WIN32)
TEST_CASE ("clap gui: parented to a window, the editor is a child of it with the size it was told, in physical pixels")
{
    const auto parent = CreateWindowExW (0, L"STATIC", L"oqa clap gui test", WS_OVERLAPPEDWINDOW,
                                         CW_USEDEFAULT, CW_USEDEFAULT, 1200, 700, nullptr, nullptr, GetModuleHandleW (nullptr), nullptr);
    REQUIRE (parent != nullptr);

    {
        Ready r;
        REQUIRE (r.gui->create (r.plugin.get(), platformApi(), false));

        clap_window_t window {};
        window.api = CLAP_WINDOW_API_WIN32;
        window.win32 = parent;
        REQUIRE (r.gui->set_parent (r.plugin.get(), &window));
        REQUIRE (r.gui->show (r.plugin.get()));

        const auto child = GetWindow (parent, GW_CHILD);
        REQUIRE (child != nullptr);

        /*  The size CLAP sees and the window's own agree, whatever the
            monitor's scale; adjust_size leaves a legal size alone. */
        auto [w, h] = r.size();
        RECT rect {};
        REQUIRE (GetWindowRect (child, &rect));
        CHECK (static_cast<uint32_t> (rect.right - rect.left) == w);
        CHECK (static_cast<uint32_t> (rect.bottom - rect.top) == h);

        uint32_t aw = w, ah = h;
        REQUIRE (r.gui->adjust_size (r.plugin.get(), &aw, &ah));
        CHECK (aw == w);
        CHECK (ah == h);

        /*  A size of the host's choosing goes through adjust_size first, as
            CLAP's drag-resize sequence does; on a monitor scaled above 100%
            the logical minimum may raise it (1.75 on the author's monitor:
            800x480 became 1260x735). */
        uint32_t want_w = 800, want_h = 480;
        REQUIRE (r.gui->adjust_size (r.plugin.get(), &want_w, &want_h));
        REQUIRE (r.gui->set_size (r.plugin.get(), want_w, want_h));
        std::tie (w, h) = r.size();
        CHECK (w == want_w);
        CHECK (h == want_h);
        REQUIRE (GetWindowRect (child, &rect));
        CHECK (static_cast<uint32_t> (rect.right - rect.left) == want_w);
        CHECK (static_cast<uint32_t> (rect.bottom - rect.top) == want_h);

        REQUIRE (r.gui->hide (r.plugin.get()));
        CHECK_FALSE (IsWindowVisible (child));
        REQUIRE (r.gui->show (r.plugin.get()));

        r.gui->destroy (r.plugin.get());
        CHECK (GetWindow (parent, GW_CHILD) == nullptr);
    }

    DestroyWindow (parent);
}
#endif
