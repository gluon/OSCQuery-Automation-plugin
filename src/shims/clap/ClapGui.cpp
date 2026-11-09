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
    THE GUI EXTENSION (Phase 5b-4): the one editor (editor::EditorRoot, a
    plain juce::Component since 5b-0; editor::MonitorRoot for the Monitor,
    6c) in the host's window, embedded, on win32, cocoa and x11. Sizes cross the API as CLAP defines them per
    window api: physical pixels on win32 and x11, logical on cocoa
    (clap/ext/gui.h); JUCE's peer knows the scale once there is a parent,
    before that the two agree. set_scale is declined: JUCE scales from the
    system, as it does inside JUCE's own wrappers.
*/

#include "ClapPlugin.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace oqa::clapshim
{
    namespace
    {
        constexpr const char* platformApi()
        {
          #if defined (_WIN32)
            return CLAP_WINDOW_API_WIN32;
          #elif defined (__APPLE__)
            return CLAP_WINDOW_API_COCOA;
          #else
            return CLAP_WINDOW_API_X11;
          #endif
        }

    }

    double ClapPlugin::guiScale() const noexcept
    {
        if (editor != nullptr)
            if (const auto* peer = editor->getPeer())
                return peer->getPlatformScaleFactor();

        return 1.0;
    }

    bool ClapPlugin::guiIsApiSupported (const char* api, bool isFloating) noexcept
    {
        if (isFloating || api == nullptr || std::strcmp (api, platformApi()) != 0)
            return false;

      #if defined (__linux__)
        /*  JUCE's descriptors need watching: the host's posix-fd support, or
            its timer for the poll. */
        return _host.canUsePosixFdSupport() || _host.canUseTimerSupport();
      #else
        return true;
      #endif
    }

    bool ClapPlugin::guiGetPreferredApi (const char** api, bool* isFloating) noexcept
    {
        *api = platformApi();
        *isFloating = false;
        return true;
    }

    bool ClapPlugin::guiCreate (const char* api, bool isFloating) noexcept
    {
        if (! guiIsApiSupported (api, isFloating) || juceInit == nullptr)
            return false;

        const auto coreNote = loaded.shared ? "Shared core library: " + juce::String (loaded.path) : juce::String (loaded.note);

        if (count == 0)
        {
            using Root = editor::MonitorRoot;
            editor = std::make_unique<Root> (editor::MonitorHost { core, coreNote });
            editorLimits = { Root::minimumWidth, Root::minimumHeight, Root::maximumWidth, Root::maximumHeight };
        }
        else
        {
            using Root = editor::EditorRoot;
            editor::Host host {
                core,
                [this] { bindingsChanged(); },
                [this] (int slot) { return slot >= 0 && slot < count ? values[static_cast<std::size_t> (slot)].load (std::memory_order_relaxed) : 0.0f; },
                coreNote
            };

            editor = std::make_unique<Root> (std::move (host));
            editorLimits = { Root::minimumWidth, Root::minimumHeight, Root::maximumWidth, Root::maximumHeight };
        }

        editor->setOpaque (true);
        return true;
    }

    void ClapPlugin::guiDestroy() noexcept
    {
        if (editor != nullptr)
        {
            editor->removeFromDesktop();
            editor.reset();
        }
    }

    bool ClapPlugin::guiSetScale (double) noexcept
    {
        return false;
    }

    bool ClapPlugin::guiGetSize (std::uint32_t* width, std::uint32_t* height) noexcept
    {
        if (editor == nullptr)
            return false;

        const auto s = guiScale();
        *width  = static_cast<std::uint32_t> (std::lround (editor->getWidth() * s));
        *height = static_cast<std::uint32_t> (std::lround (editor->getHeight() * s));
        return true;
    }

    bool ClapPlugin::guiCanResize() const noexcept
    {
        return true;
    }

    bool ClapPlugin::guiGetResizeHints (clap_gui_resize_hints_t* hints) noexcept
    {
        hints->can_resize_horizontally = true;
        hints->can_resize_vertically = true;
        hints->preserve_aspect_ratio = false;
        hints->aspect_ratio_width = 0;
        hints->aspect_ratio_height = 0;
        return true;
    }

    bool ClapPlugin::guiAdjustSize (std::uint32_t* width, std::uint32_t* height) noexcept
    {
        if (editor == nullptr)
            return false;

        /*  To logical pixels, within the editor's limits, back. */
        const auto s = guiScale();
        const auto w = std::clamp (static_cast<int> (std::lround (*width / s)),  editorLimits.minimumWidth,  editorLimits.maximumWidth);
        const auto h = std::clamp (static_cast<int> (std::lround (*height / s)), editorLimits.minimumHeight, editorLimits.maximumHeight);
        *width  = static_cast<std::uint32_t> (std::lround (w * s));
        *height = static_cast<std::uint32_t> (std::lround (h * s));
        return true;
    }

    bool ClapPlugin::guiSetSize (std::uint32_t width, std::uint32_t height) noexcept
    {
        if (editor == nullptr)
            return false;

        const auto s = guiScale();
        editor->setSize (static_cast<int> (std::lround (width / s)), static_cast<int> (std::lround (height / s)));
        return true;
    }

    bool ClapPlugin::guiSetParent (const clap_window_t* window) noexcept
    {
        if (editor == nullptr || window == nullptr)
            return false;

      #if defined (_WIN32)
        void* parent = window->win32;
      #elif defined (__APPLE__)
        void* parent = window->cocoa;
      #else
        void* parent = reinterpret_cast<void*> (static_cast<std::uintptr_t> (window->x11));
      #endif

        if (parent == nullptr)
            return false;

        editor->setVisible (true);
        editor->addToDesktop (0, parent);
        return true;
    }

    bool ClapPlugin::guiShow() noexcept
    {
        if (editor == nullptr)
            return false;

        editor->setVisible (true);
        return true;
    }

    bool ClapPlugin::guiHide() noexcept
    {
        if (editor == nullptr)
            return false;

        editor->setVisible (false);
        return true;
    }

    //==============================================================================
    void ClapPlugin::startJuce() noexcept
    {
        /*  JUCE initialised on the host's main thread, which init() is: the
            MessageManager takes the thread that creates it as the message
            thread. One per plugin, counted by JUCE, as JUCE's own wrappers
            hold one each. */
        juceInit = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
        juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();

      #if defined (__linux__)
        if (_host.canUsePosixFdSupport())
            pump = std::make_unique<LinuxEventPump> (
                [this] (int fd) { return _host.posixFdSupportRegister (fd, CLAP_POSIX_FD_READ); },
                [this] (int fd) { _host.posixFdSupportUnregister (fd); });
        else
            pump = std::make_unique<LinuxEventPump> (nullptr, nullptr);
      #endif
    }

    void ClapPlugin::stopJuce() noexcept
    {
        guiDestroy();
      #if defined (__linux__)
        pump.reset();
      #endif
        juceInit.reset();
    }

  #if defined (__linux__)
    void ClapPlugin::onPosixFd (int fd, clap_posix_fd_flags_t) noexcept
    {
        if (pump != nullptr)
            pump->onFd (fd);
    }

    void ClapPlugin::pumpJuceIfUnwatched() noexcept
    {
        if (pump != nullptr && ! _host.canUsePosixFdSupport())
            pump->pumpAll();
    }
  #endif
}
