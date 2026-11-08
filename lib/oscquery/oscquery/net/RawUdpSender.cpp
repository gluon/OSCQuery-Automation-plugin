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

#include <oscquery/net/RawUdpSender.h>

#include <oscquery/net/SocketBuffers.h>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <winsock2.h>
 #include <ws2tcpip.h>
#else
 #include <arpa/inet.h>
 #include <cerrno>
 #include <fcntl.h>
 #include <netdb.h>
 #include <netinet/in.h>
 #include <sys/socket.h>
 #include <unistd.h>
#endif

#include <cstring>

namespace oqa::oscquery::net
{
    namespace
    {
      #if defined (_WIN32)
        using Handle = SOCKET;
        constexpr Handle invalidHandle = INVALID_SOCKET;
        using AddrLen = int;

        void closeHandle (Handle h) noexcept  { ::closesocket (h); }
        int lastError() noexcept               { return ::WSAGetLastError(); }
        bool wouldBlock (int e) noexcept       { return e == WSAEWOULDBLOCK; }

        bool setNonBlocking (Handle h) noexcept
        {
            u_long on = 1;
            return ::ioctlsocket (h, FIONBIO, &on) == 0;
        }

        /*  Winsock wants WSAStartup before the first socket. JUCE does it for
            its own sockets; this sender may be the first in a process, so it
            asks too. The calls are counted by the system and paired in close(). */
        bool startSockets() noexcept
        {
            WSADATA data;
            return ::WSAStartup (MAKEWORD (2, 2), &data) == 0;
        }

        void stopSockets() noexcept { ::WSACleanup(); }
      #else
        using Handle = int;
        constexpr Handle invalidHandle = -1;
        using AddrLen = socklen_t;

        void closeHandle (Handle h) noexcept  { ::close (h); }
        int lastError() noexcept               { return errno; }
        bool wouldBlock (int e) noexcept       { return e == EAGAIN || e == EWOULDBLOCK; }

        bool setNonBlocking (Handle h) noexcept
        {
            const int flags = ::fcntl (h, F_GETFL, 0);
            return flags >= 0 && ::fcntl (h, F_SETFL, flags | O_NONBLOCK) == 0;
        }

        bool startSockets() noexcept { return true; }
        void stopSockets() noexcept {}
      #endif

        std::string textOf (const sockaddr* address, AddrLen length)
        {
            char host[NI_MAXHOST] = {};
            char service[NI_MAXSERV] = {};

            if (::getnameinfo (address, length, host, sizeof host, service, sizeof service,
                               NI_NUMERICHOST | NI_NUMERICSERV) != 0)
                return {};

            return address->sa_family == AF_INET6 ? "[" + std::string (host) + "]:" + service
                                                  : std::string (host) + ":" + service;
        }
    }

    struct RawUdpSender::Impl
    {
        Handle handle = invalidHandle;
        sockaddr_storage destination {};
        AddrLen destinationLength = 0;
        bool socketsStarted = false;
    };

    //==============================================================================
    RawUdpSender::RawUdpSender() noexcept = default;

    RawUdpSender::~RawUdpSender()
    {
        close();
    }

    bool RawUdpSender::isOpen() const noexcept
    {
        return impl != nullptr && impl->handle != invalidHandle;
    }

    bool RawUdpSender::open (const std::string& host, int port, std::string& error)
    {
        close();

        if (host.empty() || port <= 0 || port > 65535)
        {
            error = "a host and a port in 1..65535 are needed";
            return false;
        }

        auto* state = new Impl();
        state->socketsStarted = startSockets();

        if (! state->socketsStarted)
        {
            error = "the system's sockets would not start";
            delete state;
            return false;
        }

        addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        addrinfo* found = nullptr;
        const auto portText = std::to_string (port);

        if (const int rc = ::getaddrinfo (host.c_str(), portText.c_str(), &hints, &found); rc != 0 || found == nullptr)
        {
            error = "\"" + host + "\" does not resolve";
            stopSockets();
            delete state;
            return false;
        }

        /*  IPv4 first, as the devices on the bench are; else whatever came. */
        const addrinfo* chosen = found;

        for (const auto* a = found; a != nullptr; a = a->ai_next)
            if (a->ai_family == AF_INET) { chosen = a; break; }

        std::memcpy (&state->destination, chosen->ai_addr, static_cast<std::size_t> (chosen->ai_addrlen));
        state->destinationLength = static_cast<AddrLen> (chosen->ai_addrlen);
        const int family = chosen->ai_family;
        ::freeaddrinfo (found);

        state->handle = ::socket (family, SOCK_DGRAM, IPPROTO_UDP);

        if (state->handle == invalidHandle)
        {
            error = "no UDP socket could be made (error " + std::to_string (lastError()) + ")";
            stopSockets();
            delete state;
            return false;
        }

        if (! setNonBlocking (state->handle))
        {
            error = "the socket would not go non-blocking (error " + std::to_string (lastError()) + ")";
            closeHandle (state->handle);
            stopSockets();
            delete state;
            return false;
        }

        sendBytes = askForBuffers (static_cast<int> (state->handle), 0, sendBufferWanted).send;
        resolved = textOf (reinterpret_cast<const sockaddr*> (&state->destination), state->destinationLength);

        sentCount.store (0, std::memory_order_relaxed);
        droppedCount.store (0, std::memory_order_relaxed);
        failureCount.store (0, std::memory_order_relaxed);

        impl = state;
        return true;
    }

    void RawUdpSender::close() noexcept
    {
        if (impl == nullptr)
            return;

        if (impl->handle != invalidHandle)
            closeHandle (impl->handle);

        if (impl->socketsStarted)
            stopSockets();

        delete impl;
        impl = nullptr;
        sendBytes = 0;
        resolved.clear();
    }

    bool RawUdpSender::send (const std::uint8_t* data, std::size_t size) noexcept
    {
        if (! isOpen() || data == nullptr || size == 0)
            return false;

      #if defined (_WIN32)
        const auto written = ::sendto (impl->handle, reinterpret_cast<const char*> (data), static_cast<int> (size), 0,
                                       reinterpret_cast<const sockaddr*> (&impl->destination), impl->destinationLength);
        const bool ok = written != SOCKET_ERROR && static_cast<std::size_t> (written) == size;
      #else
        const auto written = ::sendto (impl->handle, data, size, MSG_DONTWAIT,
                                       reinterpret_cast<const sockaddr*> (&impl->destination), impl->destinationLength);
        const bool ok = written >= 0 && static_cast<std::size_t> (written) == size;
      #endif

        if (ok)
        {
            sentCount.fetch_add (1, std::memory_order_relaxed);
            return true;
        }

        if (wouldBlock (lastError()))
            droppedCount.fetch_add (1, std::memory_order_relaxed);
        else
            failureCount.fetch_add (1, std::memory_order_relaxed);

        return false;
    }
}
