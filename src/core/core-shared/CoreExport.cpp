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
    THE SHARED CORE LIBRARY'S ONE EXPORT. The table it answers is the same
    one a plugin binary holds statically; what differs is the registry behind
    it, which here is the one the whole process shares.
*/

#define OQA_CORE_BUILDING_SHARED 1

#include <core/registry/RegistryApi.h>

extern "C" OQA_CORE_EXPORT const OqaCoreApi* oqa_core_api (void)
{
    return oqa::core::staticCoreApi();
}
