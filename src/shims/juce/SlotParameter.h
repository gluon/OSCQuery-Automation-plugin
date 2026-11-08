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

#pragma once

/*
    ONE SLOT AS THE HOST SEES IT: a normalised 0-1 float with a stable ID
    (PRD, Binding model: "Host parameter ID: fixed at build time, stable
    across binds, renames and sessions; automation curves attach to it") and a
    name that follows the binding. Unbound, it is named "(unbound)" and never
    hidden (VST3 cannot, and AU must not: decided 2026-11-08 after the Phase 1
    bench).

    THE HOST READS getName AND getText ON ITS OWN THREADS, so what they show is
    a copy refreshed by the processor from the core's slot whenever the
    bindings change, under a spin lock the audio thread never takes: the audio
    thread reads getValue() only, an atomic. getText shows the device's units,
    scaled by the slot's own curve, so a lane in REAPER reads "-12.0 dB" and
    not "0.4".

    The Phase 1 spike's class (git 6f3012e^:spikes/phase1_formats), without
    the hidden flag.
*/

#include <core/Slot.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace oqa::shim
{
    class SlotParameter final : public juce::AudioProcessorParameterWithID
    {
    public:
        explicit SlotParameter (int slotIndex);

        int slotIndex() const noexcept { return index; }

        float getValue() const override;
        void setValue (float newValue) override;
        float getDefaultValue() const override;
        juce::String getName (int maximumStringLength) const override;
        juce::String getLabel() const override;
        juce::String getText (float normalisedValue, int maximumStringLength) const override;
        float getValueForText (const juce::String& text) const override;
        bool isAutomatable() const override { return true; }

        /** The processor's refresh: what to show for this slot now. Message thread. */
        void follow (const core::Slot& slot);

        static const juce::String unboundName;

    private:
        const int index;
        std::atomic<float> value { 0.0f };

        mutable juce::SpinLock shownLock;
        juce::String shownName { unboundName };
        juce::String shownUnit;
        core::Slot shown;          // a copy, for getText's scaling; bound() false when unbound

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotParameter)
    };
}
