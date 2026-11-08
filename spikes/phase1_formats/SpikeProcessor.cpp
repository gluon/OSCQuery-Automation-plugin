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

#include "SpikeProcessor.h"

namespace spike
{
    //==============================================================================
    SlotParameter::SlotParameter (int slotNumber, bool hide)
        : juce::AudioProcessorParameterWithID (juce::ParameterID { "slot" + juce::String (slotNumber), 1 },
                                               unboundName),
          hideWhenUnbound (hide)
    {
    }

    float SlotParameter::getValue() const { return value.load(); }
    void SlotParameter::setValue (float newValue) { value.store (newValue); }
    float SlotParameter::getDefaultValue() const { return 0.0f; }

    juce::String SlotParameter::getName (int maximumStringLength) const
    {
        return currentName().substring (0, maximumStringLength);
    }

    juce::String SlotParameter::getText (float normalisedValue, int maximumStringLength) const
    {
        return juce::String (normalisedValue, 3).substring (0, maximumStringLength);
    }

    float SlotParameter::getValueForText (const juce::String& text) const { return text.getFloatValue(); }
    bool SlotParameter::isHidden() const { return hideWhenUnbound && ! bound.load(); }
    bool SlotParameter::isBound() const { return bound.load(); }

    juce::String SlotParameter::currentName() const
    {
        const juce::SpinLock::ScopedLockType lock (nameLock);
        return name;
    }

    void SlotParameter::bind (const juce::String& newName)
    {
        {
            const juce::SpinLock::ScopedLockType lock (nameLock);
            name = newName;
        }
        bound.store (true);
    }

    void SlotParameter::unbind()
    {
        {
            const juce::SpinLock::ScopedLockType lock (nameLock);
            name = unboundName;
        }
        bound.store (false);
    }

    //==============================================================================
    class SpikeEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
    {
    public:
        explicit SpikeEditor (SpikeProcessor& p) : juce::AudioProcessorEditor (p), processor (p)
        {
            for (int i = 0; i < numSlots; ++i)
            {
                auto& row = rows[(size_t) i];
                row.label.setText ("Slot " + juce::String (i + 1), juce::dontSendNotification);
                row.field.setText ("Device " + juce::String (i < 4 ? "A" : "B") + " /param/" + juce::String (i + 1));
                row.bind.setButtonText ("Bind");
                row.unbind.setButtonText ("Unbind");
                row.bind.onClick = [this, i] { processor.bind (i, rows[(size_t) i].field.getText()); refresh(); };
                row.unbind.onClick = [this, i] { processor.unbind (i); refresh(); };

                for (auto* c : { (juce::Component*) &row.label, (juce::Component*) &row.field,
                                 (juce::Component*) &row.bind, (juce::Component*) &row.unbind,
                                 (juce::Component*) &row.state })
                    addAndMakeVisible (c);
            }

            bindAll.setButtonText ("Bind all");
            unbindAll.setButtonText ("Unbind all");
            bindAll.onClick = [this]
            {
                for (int i = 0; i < numSlots; ++i)
                    processor.bind (i, rows[(size_t) i].field.getText());
                refresh();
            };
            unbindAll.onClick = [this]
            {
                for (int i = 0; i < numSlots; ++i)
                    processor.unbind (i);
                refresh();
            };
            addAndMakeVisible (bindAll);
            addAndMakeVisible (unbindAll);

            footer.setText (juce::String (SpikeProcessor::hidesUnboundSlots() ? "Hiding unbound slots (AU). " : "Not hiding unbound slots (no-hide). ")
                            + "Commands also read from " + SpikeProcessor::commandDirectory().getFullPathName(),
                            juce::dontSendNotification);
            footer.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (footer);

            setSize (620, 40 + numSlots * 30 + 70);
            refresh();
            startTimerHz (4);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (15.0f));
            g.drawText ("OQA Spike 8 - Phase 1 format bench (throwaway)", 10, 6, getWidth() - 20, 24,
                        juce::Justification::centredLeft);
        }

        void resized() override
        {
            int y = 40;

            for (auto& row : rows)
            {
                row.label.setBounds (10, y, 60, 24);
                row.field.setBounds (75, y, 230, 24);
                row.bind.setBounds (312, y, 60, 24);
                row.unbind.setBounds (378, y, 60, 24);
                row.state.setBounds (445, y, getWidth() - 455, 24);
                y += 30;
            }

            bindAll.setBounds (75, y + 6, 100, 26);
            unbindAll.setBounds (182, y + 6, 100, 26);
            footer.setBounds (10, y + 38, getWidth() - 20, 20);
        }

    private:
        struct Row
        {
            juce::Label label, state;
            juce::TextEditor field;
            juce::TextButton bind, unbind;
        };

        void timerCallback() override { refresh(); }

        void refresh()
        {
            for (int i = 0; i < numSlots; ++i)
            {
                auto& s = processor.slot (i);
                rows[(size_t) i].state.setText ((s.isBound() ? "bound: " : "hidden: ") + s.currentName(),
                                                juce::dontSendNotification);
            }
        }

        SpikeProcessor& processor;
        std::array<Row, numSlots> rows;
        juce::TextButton bindAll, unbindAll;
        juce::Label footer;
    };

    //==============================================================================
    SpikeProcessor::SpikeProcessor()
        : juce::AudioProcessor (BusesProperties()
                                    .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        for (int group = 0; group < 2; ++group)
        {
            auto tree = std::make_unique<juce::AudioProcessorParameterGroup> (
                group == 0 ? "deviceA" : "deviceB", group == 0 ? "Device A" : "Device B", " | ");

            for (int i = group * 4; i < group * 4 + 4; ++i)
            {
                auto param = std::make_unique<SlotParameter> (i + 1, hidesUnboundSlots());
                slots[(size_t) i] = param.get();
                tree->addChild (std::move (param));
            }

            addParameterGroup (std::move (tree));
        }

        //  A command left over from an earlier run is not replayed.
        const auto existing = commandDirectory().getChildFile ("command.txt");
        if (existing.existsAsFile())
            lastSequence = existing.loadFileAsString().upToFirstOccurrenceOf ("\n", false, false)
                               .fromFirstOccurrenceOf ("seq ", false, false).getIntValue();

        startTimer (50);
    }

    SpikeProcessor::~SpikeProcessor() { stopTimer(); }

    const juce::String SpikeProcessor::getName() const { return JucePlugin_Name; }

    bool SpikeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        return ! layouts.getMainOutputChannelSet().isDisabled()
               && layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
    }

    juce::AudioProcessorEditor* SpikeProcessor::createEditor() { return new SpikeEditor (*this); }

    juce::File SpikeProcessor::commandDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".oqa-spike-phase1");
    }

    bool SpikeProcessor::hidesUnboundSlots()
    {
        return ! commandDirectory().getChildFile ("no-hide").existsAsFile();
    }

    void SpikeProcessor::tellHost()
    {
        updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
    }

    void SpikeProcessor::bind (int slotIndex, const juce::String& name)
    {
        slot (slotIndex).bind (name);
        tellHost();
    }

    void SpikeProcessor::unbind (int slotIndex)
    {
        slot (slotIndex).unbind();
        tellHost();
    }

    void SpikeProcessor::timerCallback()
    {
        const auto file = commandDirectory().getChildFile ("command.txt");

        if (! file.existsAsFile())
            return;

        juce::StringArray lines;
        lines.addLines (file.loadFileAsString());
        lines.removeEmptyStrings();

        if (lines.isEmpty() || ! lines[0].startsWith ("seq "))
            return;

        const auto sequence = lines[0].substring (4).getIntValue();

        if (sequence == lastSequence)
            return;

        lastSequence = sequence;
        lines.remove (0);
        applyCommands (lines);
        writeAck (sequence);
    }

    void SpikeProcessor::applyCommands (const juce::StringArray& lines)
    {
        for (const auto& line : lines)
        {
            const auto verb = line.upToFirstOccurrenceOf (" ", false, false);
            const auto rest = line.fromFirstOccurrenceOf (" ", false, false).trim();
            const auto index = rest.getIntValue() - 1;
            const bool validIndex = index >= 0 && index < numSlots;

            if (verb == "bind" && validIndex)
                slot (index).bind (rest.fromFirstOccurrenceOf (" ", false, false).trim());
            else if (verb == "unbind" && validIndex)
                slot (index).unbind();
            else if (verb == "bindall")
                for (int i = 0; i < numSlots; ++i)
                    slot (i).bind (rest + " " + juce::String (i + 1));
            else if (verb == "unbindall")
                for (int i = 0; i < numSlots; ++i)
                    slot (i).unbind();
        }

        tellHost();
    }

    void SpikeProcessor::writeAck (int sequence)
    {
        juce::String text;
        text << "seq " << sequence << "\n";

        for (int i = 0; i < numSlots; ++i)
            text << "slot " << (i + 1) << "\t" << (slot (i).isBound() ? "bound" : "hidden") << "\t"
                 << slot (i).currentName() << "\n";

        const auto dir = commandDirectory();
        const auto temp = dir.getChildFile ("ack.tmp");
        temp.replaceWithText (text);
        temp.moveFileTo (dir.getChildFile ("ack.txt"));
    }

    void SpikeProcessor::getStateInformation (juce::MemoryBlock& destination)
    {
        juce::XmlElement xml ("OQA_SPIKE8");

        for (int i = 0; i < numSlots; ++i)
        {
            auto* e = xml.createNewChildElement ("slot");
            e->setAttribute ("bound", slot (i).isBound());
            e->setAttribute ("name", slot (i).currentName());
            e->setAttribute ("value", (double) slot (i).getValue());
        }

        copyXmlToBinary (xml, destination);
    }

    void SpikeProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        const auto xml = getXmlFromBinary (data, sizeInBytes);

        if (xml == nullptr || ! xml->hasTagName ("OQA_SPIKE8"))
            return;

        int i = 0;

        for (auto* e : xml->getChildIterator())
        {
            if (i >= numSlots)
                break;

            if (e->getBoolAttribute ("bound"))
                slot (i).bind (e->getStringAttribute ("name"));
            else
                slot (i).unbind();

            slot (i).setValue ((float) e->getDoubleAttribute ("value"));
            ++i;
        }

        tellHost();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new spike::SpikeProcessor();
}
