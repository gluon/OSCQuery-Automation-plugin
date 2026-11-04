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

#include "PluginProcessor.h"

// The five plugin IDs are five slot counts (PRD, Binding model). Phase 0 builds one.
static_assert (OQA_SLOT_COUNT == 8 || OQA_SLOT_COUNT == 16 || OQA_SLOT_COUNT == 32
                   || OQA_SLOT_COUNT == 64 || OQA_SLOT_COUNT == 128,
               "OQA_SLOT_COUNT must be one of the five variants");

namespace oqa::shim
{
    PluginProcessor::PluginProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    const juce::String PluginProcessor::getName() const
    {
        return JucePlugin_Name;
    }

    bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        // Pass-through: whatever comes in goes out, so the two main buses must
        // match, and must not be disabled.
        const auto& in = layouts.getMainInputChannelSet();
        const auto& out = layouts.getMainOutputChannelSet();
        return ! out.isDisabled() && in == out;
    }

    void PluginProcessor::prepareToPlay (double, int) {}
    void PluginProcessor::releaseResources() {}

    void PluginProcessor::processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&)
    {
        // Nothing. Input and output share the buffer and have the same layout,
        // so leaving it alone is the pass-through.
    }

    bool PluginProcessor::hasEditor() const { return false; }
    juce::AudioProcessorEditor* PluginProcessor::createEditor() { return nullptr; }

    bool PluginProcessor::acceptsMidi() const { return false; }
    bool PluginProcessor::producesMidi() const { return false; }
    double PluginProcessor::getTailLengthSeconds() const { return 0.0; }

    int PluginProcessor::getNumPrograms() { return 1; }
    int PluginProcessor::getCurrentProgram() { return 0; }
    void PluginProcessor::setCurrentProgram (int) {}
    const juce::String PluginProcessor::getProgramName (int) { return {}; }
    void PluginProcessor::changeProgramName (int, const juce::String&) {}

    // No state yet. The state chunk (PRD, Session persistence) is Phase 3's, and
    // carries a version from its first commit.
    void PluginProcessor::getStateInformation (juce::MemoryBlock&) {}
    void PluginProcessor::setStateInformation (const void*, int) {}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new oqa::shim::PluginProcessor();
}
