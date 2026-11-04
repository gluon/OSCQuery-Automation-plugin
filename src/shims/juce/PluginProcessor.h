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
    THE JUCE SHIM'S PROCESSOR, PHASE 0: a plugin that does nothing, on purpose.

    Audio passes through untouched (PRD non-goal: no audio processing in the
    plugin), there are no parameters yet, no editor and no state. It exists so
    that CI builds, validates and uploads a real VST3 (and AU on macOS) from the
    first commit, and so that the plugin's identity is fixed before anything is
    saved against it. The slots arrive in Phase 3, through the core.
*/

#include <juce_audio_processors/juce_audio_processors.h>

namespace oqa::shim
{
    class PluginProcessor final : public juce::AudioProcessor
    {
    public:
        PluginProcessor();

        const juce::String getName() const override;

        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void prepareToPlay (double sampleRate, int maximumBlockSize) override;
        void releaseResources() override;

        void processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override;
        using juce::AudioProcessor::processBlock;

        bool hasEditor() const override;
        juce::AudioProcessorEditor* createEditor() override;

        bool acceptsMidi() const override;
        bool producesMidi() const override;
        double getTailLengthSeconds() const override;

        int getNumPrograms() override;
        int getCurrentProgram() override;
        void setCurrentProgram (int index) override;
        const juce::String getProgramName (int index) override;
        void changeProgramName (int index, const juce::String& name) override;

        void getStateInformation (juce::MemoryBlock& destination) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
    };
}
