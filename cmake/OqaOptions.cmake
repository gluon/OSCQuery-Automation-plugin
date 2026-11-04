# This file is part of the OSCQuery Automation Plugin —
# https://github.com/pob31/OSCQuery-Automation-plugin
#
# Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle
#
# The OSCQuery Automation Plugin is free software: you can redistribute it
# and/or modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation, either version 3 of the License,
# or (at your option) any later version. It is distributed in the hope that it
# will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
# Public License (LICENSE, at the repository root) for more details.
#
# SPDX-License-Identifier: GPL-3.0-or-later

# Every option() of the build, in one place.

option(OQA_BUILD_TESTS
    "Build the test suite, the mock OSCQuery device and its command-line runner" ON)

# Reaches oqa::warnings only, which is linked to our own targets and never to vendor
# sources, so a JUCE header warning under a future compiler cannot redden the build.
option(OQA_WARNINGS_AS_ERRORS "Treat warnings in this project's own code as errors" OFF)

# The French locale the suite runs a second time under (devplan, cross-cutting: everything
# that serialises numbers runs under fr_FR as well as C). The Windows UCRT spells it
# "fr-FR" and glibc and macOS "fr_FR.UTF-8"; this is the one place that difference is
# allowed to exist.
if(WIN32)
    set(_oqa_locale_fr_default "fr-FR")
else()
    set(_oqa_locale_fr_default "fr_FR.UTF-8")
endif()
set(OQA_LOCALE_FR "${_oqa_locale_fr_default}" CACHE STRING
    "Name of the French locale as this platform's setlocale() spells it")
unset(_oqa_locale_fr_default)
