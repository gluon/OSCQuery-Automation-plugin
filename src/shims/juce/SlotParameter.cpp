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

#include "SlotParameter.h"

namespace oqa::shim
{
    const juce::String SlotParameter::unboundName { "(unbound)" };

    namespace
    {
        /*  "slot01".."slot128": the ID the host stores curves against. The
            version hint is 1 and never changes. */
        juce::ParameterID idFor (int slotIndex)
        {
            return { "slot" + juce::String (slotIndex + 1).paddedLeft ('0', 2), 1 };
        }
    }

    SlotParameter::SlotParameter (int slotIndex)
        : juce::AudioProcessorParameterWithID (idFor (slotIndex), unboundName),
          index (slotIndex)
    {
    }

    float SlotParameter::getValue() const                 { return value.load (std::memory_order_relaxed); }
    void SlotParameter::setValue (float newValue)         { value.store (newValue, std::memory_order_relaxed); }
    float SlotParameter::getDefaultValue() const          { return 0.0f; }

    juce::String SlotParameter::getName (int maximumStringLength) const
    {
        const juce::SpinLock::ScopedLockType guard (shownLock);
        return shownName.substring (0, maximumStringLength);
    }

    juce::String SlotParameter::getLabel() const
    {
        const juce::SpinLock::ScopedLockType guard (shownLock);
        return shownUnit;
    }

    juce::String SlotParameter::getText (float normalisedValue, int maximumStringLength) const
    {
        const juce::SpinLock::ScopedLockType guard (shownLock);

        if (! shown.active())
            return juce::String (normalisedValue, 3).substring (0, maximumStringLength);

        if (shown.isToggle())
            return juce::String (normalisedValue >= 0.5f ? "on" : "off");

        const auto units = shown.toDevice (normalisedValue);
        const auto text = shown.isInteger() ? juce::String (static_cast<int> (units)) : juce::String (units, 3);
        return text.substring (0, maximumStringLength);
    }

    float SlotParameter::getValueForText (const juce::String& text) const
    {
        const juce::SpinLock::ScopedLockType guard (shownLock);

        if (! shown.active())
            return juce::jlimit (0.0f, 1.0f, text.getFloatValue());

        if (shown.isToggle())
            return (text.trim().equalsIgnoreCase ("on") || text.getFloatValue() >= 0.5f) ? 1.0f : 0.0f;

        return shown.toHost (text.getDoubleValue());
    }

    void SlotParameter::follow (const core::Slot& slot)
    {
        const juce::SpinLock::ScopedLockType guard (shownLock);
        shown = slot;
        shownName = slot.bound() ? juce::String (slot.displayName) : unboundName;
        shownUnit = juce::String (slot.unit);
    }
}
