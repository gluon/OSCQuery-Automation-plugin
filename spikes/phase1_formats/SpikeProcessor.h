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
    SPIKE, PHASE 1: the PRD's parameter model in eight slots, and nothing else.

    Each slot is one host parameter: a normalised float with a stable ID
    ("slot1" ... "slot8"), whose NAME changes at runtime. Unbound, a slot is
    named "(unbound)" and reports isHidden() (the patched JUCE turns that into
    AU's IsHidden; VST3 has no such flag). Binding or unbinding calls
    updateHostDisplay with parameterInfoChanged, which JUCE turns into
    kParamTitlesChanged (VST3) and a ParameterList/ParameterInfo change (AU).
    Slots 1-4 and 5-8 are two parameter groups, i.e. two AU clumps and two
    VST3 units, standing in for two devices.

    Two ways to bind, because the bench has hosts driven by hand and one
    driven by script:
      - the editor: a name field, Bind and Unbind per slot;
      - a command file, ~/.oqa-spike-phase1/command.txt, polled every 50 ms:
            seq <n>
            bind <slot> <name...>
            unbind <slot>
            bindall <prefix>
            unbindall
        applied once per new <n>, acknowledged in ack.txt with every slot's
        name and hidden state. REAPER's bench script writes it.
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>

namespace spike
{
    inline constexpr int numSlots = 8;
    inline const juce::String unboundName { "(unbound)" };

    class SlotParameter final : public juce::AudioProcessorParameterWithID
    {
    public:
        explicit SlotParameter (int slotNumber);

        float getValue() const override;
        void setValue (float newValue) override;
        float getDefaultValue() const override;
        juce::String getName (int maximumStringLength) const override;
        juce::String getText (float normalisedValue, int maximumStringLength) const override;
        float getValueForText (const juce::String& text) const override;
        bool isHidden() const override;   // from juce-au-ishidden.patch

        void bind (const juce::String& newName);
        void unbind();
        bool isBound() const;
        juce::String currentName() const;

    private:
        std::atomic<float> value { 0.0f };
        std::atomic<bool> bound { false };
        mutable juce::SpinLock nameLock;
        juce::String name { unboundName };
    };

    class SpikeProcessor final : public juce::AudioProcessor,
                                 private juce::Timer
    {
    public:
        SpikeProcessor();
        ~SpikeProcessor() override;

        const juce::String getName() const override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        using juce::AudioProcessor::processBlock;

        bool hasEditor() const override { return true; }
        juce::AudioProcessorEditor* createEditor() override;
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destination) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        //  Message thread only.
        void bind (int slotIndex, const juce::String& name);
        void unbind (int slotIndex);
        SlotParameter& slot (int slotIndex) { return *slots[(size_t) slotIndex]; }

        static juce::File commandDirectory();

    private:
        void timerCallback() override;
        void applyCommands (const juce::StringArray& lines);
        void writeAck (int sequence);
        void tellHost();

        std::array<SlotParameter*, numSlots> slots {};
        int lastSequence = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpikeProcessor)
    };
}
