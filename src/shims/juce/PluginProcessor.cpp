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

#include <core/rt/RtCheck.h>

// The five plugin IDs are five slot counts (PRD, Binding model). Phase 3 builds one.
static_assert (OQA_SLOT_COUNT == 8 || OQA_SLOT_COUNT == 16 || OQA_SLOT_COUNT == 32
                   || OQA_SLOT_COUNT == 64 || OQA_SLOT_COUNT == 128,
               "OQA_SLOT_COUNT must be one of the five variants");

namespace oqa::shim
{
    namespace
    {
        std::string instanceLabel()
        {
            static std::atomic<int> counter { 0 };
            return std::string (JucePlugin_Name) + " #" + std::to_string (++counter);
        }
    }

    PluginProcessor::PluginProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          loaded (core::RegistryLoader::load()),
          core (loaded.api, slotCount, instanceLabel())
    {
        slots.reserve (static_cast<std::size_t> (slotCount));

        for (int i = 0; i < slotCount; ++i)
        {
            auto parameter = std::make_unique<SlotParameter> (i);
            slots.push_back (parameter.get());
            addParameter (parameter.release());
        }

        bindingsChanged();

        /*  A slow look at the core's revision, so a binding made by anything
            but this editor (a reload, a future headless path) renames too. */
        startTimer (250);
    }

    PluginProcessor::~PluginProcessor()
    {
        stopTimer();
        core.disconnect();
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

    //==============================================================================
    void PluginProcessor::prepareToPlay (double, int)
    {
        core.connect();
    }

    void PluginProcessor::releaseResources()
    {
        core.disconnect();
    }

    void PluginProcessor::processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&)
    {
        /*  Audio: nothing. Input and output share the buffer and have the same
            layout, so leaving it alone is the pass-through. */
        const core::rt::ScopedRealtimeCheck region;

        for (std::size_t i = 0; i < slots.size(); ++i)
            values[i] = slots[i]->getValue();

        core.processBlock (values.data(), slotCount);
    }

    //==============================================================================
    bool PluginProcessor::hasEditor() const { return true; }

    juce::AudioProcessorEditor* PluginProcessor::createEditor()
    {
        /*  Phase 3d: the host's generic view of the slots. The editor proper,
            with the device browser and the slot table, is Phase 3e's. */
        return new juce::GenericAudioProcessorEditor (*this);
    }

    bool PluginProcessor::acceptsMidi() const { return false; }
    bool PluginProcessor::producesMidi() const { return false; }
    double PluginProcessor::getTailLengthSeconds() const { return 0.0; }

    int PluginProcessor::getNumPrograms() { return 1; }
    int PluginProcessor::getCurrentProgram() { return 0; }
    void PluginProcessor::setCurrentProgram (int) {}
    const juce::String PluginProcessor::getProgramName (int) { return {}; }
    void PluginProcessor::changeProgramName (int, const juce::String&) {}

    //==============================================================================
    void PluginProcessor::getStateInformation (juce::MemoryBlock& destination)
    {
        const auto json = core.saveState();
        destination.replaceAll (json.data(), json.size());
    }

    void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        if (data == nullptr || sizeInBytes <= 0)
            return;

        std::string error;
        core.loadState (std::string_view (static_cast<const char*> (data), static_cast<std::size_t> (sizeInBytes)), error);
        bindingsChanged();
    }

    //==============================================================================
    void PluginProcessor::bindingsChanged()
    {
        const auto& chunk = core.chunk();

        for (auto* parameter : slots)
            parameter->follow (chunk.slots[parameter->slotIndex()]);

        shownRevision = core.revision();
        updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
    }

    void PluginProcessor::timerCallback()
    {
        if (core.revision() != shownRevision)
            bindingsChanged();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new oqa::shim::PluginProcessor();
}
