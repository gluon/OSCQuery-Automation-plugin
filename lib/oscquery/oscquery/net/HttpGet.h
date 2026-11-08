/*
    This file is part of Go.dot — https://github.com/pob31/go.dot

    Copyright (C) 2026 Pierre-Olivier Boulant

    Go.dot is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version. Go.dot is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
    (LICENSE, at the repository root) for more details.

    SPDX-License-Identifier: GPL-3.0-or-later

    Origin: go.dot src/wfg/engine/oscquery/OscQueryClient.h at 1c8aaa0a (HttpReply
    and OscQueryClient::get), as a free function in oqa::oscquery::net; readValue
    and valueFromReply left behind (the tree model reads values here).
*/

#pragma once

/*
    One HTTP GET over a raw socket, reading the status line off the wire.

    WHY IT IS WRITTEN HERE RATHER THAN REACHED FOR. `juce_simpleweb` ships a
    WebSocket client and HTTP servers, no HTTP client; and juce::URL is wrong
    for OSCQuery in two separately measured ways:

      * IT RE-ENCODES THE QUERY STRING. OSCQuery asks with a bare key -
        `?HOST_INFO`, `?VALUE` - and juce::URL turns that into `?HOST_INFO=`,
        which a conforming server does not match. WFS-DIY and go.dot both
        abandoned juce::URL in their clients for this.
      * IT NORMALISES THE STATUS. go.dot observed it reporting 200 for a 204,
        and 204 is load-bearing: it means the node is real and the attribute
        absent, which differs from 404 and from a JSON null (spec draft, §6).

    IT BLOCKS. `connect`, `write` and `waitUntilReady` are syscalls with a
    deadline attached, and a device that has gone away takes the whole deadline
    to say so. Nothing in the plugin calls it from the audio thread or the
    message thread; Device owns the thread it runs on.

    GET only, no redirects, no keep-alive, no TLS (the binary links no OpenSSL).
    It reads `Content-Length` and chunked bodies and nothing more exotic, which
    covers every OSCQuery server anybody has shipped.
*/

#include <string>

namespace oqa::oscquery::net
{
    struct HttpReply
    {
        /** The exchange completed. Says nothing about what the server thought. */
        bool ok = false;

        /*  The HTTP status, read off the wire rather than interpreted. 200
            found, 204 the node is real and this attribute is not, 400 not an
            OSCQuery attribute, 404 no such node, 405 not a GET. */
        int status = 0;

        std::string body;

        /** Why the exchange did not complete, when it did not. */
        std::string error;
    };

    /*  One GET, blocking, with a deadline in milliseconds.

        `query` is appended after a `?` VERBATIM and is never escaped, which is
        the whole reason this exists: `VALUE` must arrive as `?VALUE`. Pass it
        empty for a plain path. */
    HttpReply httpGet (const std::string& host, int port, const std::string& path,
                       const std::string& query, int timeoutMs);
}
