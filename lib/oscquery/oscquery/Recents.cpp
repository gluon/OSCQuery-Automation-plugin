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

#include <oscquery/Recents.h>

#include <juce_data_structures/juce_data_structures.h>

#include <algorithm>
#include <mutex>

namespace oqa::oscquery
{
    namespace
    {
        constexpr const char* recentsKey = "recents";

        juce::PropertiesFile::Options fileOptions()
        {
            juce::PropertiesFile::Options options;
            options.applicationName = "OSCQuery Automation Plugin";
            options.filenameSuffix = "settings";
            options.folderName = "Pix et Bel";
            options.osxLibrarySubFolder = "Application Support";
            options.commonToAllUsers = false;
            options.storageFormat = juce::PropertiesFile::storeAsXML;
            return options;
        }

        juce::var entryToVar (const Recents::Entry& entry)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty ("name", juce::String (entry.name));
            object->setProperty ("host", juce::String (entry.host));
            object->setProperty ("port", entry.port);
            object->setProperty ("last_seen_ms", entry.lastSeenMs);

            if (entry.identityPath.has_value())
                object->setProperty ("identity_path", juce::String (*entry.identityPath));

            if (entry.identityValue.has_value())
                object->setProperty ("identity_value", juce::String (*entry.identityValue));

            return juce::var (object);
        }

        std::optional<Recents::Entry> entryFromVar (const juce::var& v)
        {
            if (! v.isObject() || v.isArray() || v.getDynamicObject() == nullptr)
                return std::nullopt;

            Recents::Entry entry;
            entry.name = v.getProperty ("name", {}).toString().toStdString();
            entry.host = v.getProperty ("host", {}).toString().toStdString();
            entry.port = static_cast<int> (v.getProperty ("port", 0));
            entry.lastSeenMs = static_cast<juce::int64> (v.getProperty ("last_seen_ms", 0));

            if (v.hasProperty ("identity_path"))
                entry.identityPath = v["identity_path"].toString().toStdString();

            if (v.hasProperty ("identity_value"))
                entry.identityValue = v["identity_value"].toString().toStdString();

            if (entry.name.empty())
                return std::nullopt;

            return entry;
        }
    }

    //==============================================================================
    struct Recents::Impl
    {
        explicit Impl (Options o) : options (std::move (o))
        {
            if (options.file == juce::File())
                options.file = defaultFile();

            options.capacity = std::max (1, options.capacity);
        }

        Options options;
        mutable std::mutex lock;
        std::vector<Entry> entries;

        //  Under `lock`.
        void trim()
        {
            if (entries.size() > static_cast<std::size_t> (options.capacity))
                entries.resize (static_cast<std::size_t> (options.capacity));
        }

        juce::var toVarLocked() const
        {
            juce::Array<juce::var> list;

            for (const auto& entry : entries)
                list.add (entryToVar (entry));

            return list;
        }

        bool fromVarLocked (const juce::var& list)
        {
            const auto* array = list.getArray();

            if (array == nullptr)
                return false;

            std::vector<Entry> fresh;

            for (const auto& item : *array)
                if (auto entry = entryFromVar (item))
                    fresh.push_back (std::move (*entry));

            entries = std::move (fresh);
            trim();
            return true;
        }

        bool saveLocked()
        {
            juce::PropertiesFile file (options.file, fileOptions());
            file.setValue (recentsKey, juce::JSON::toString (toVarLocked(), true));
            return file.save();
        }
    };

    //==============================================================================
    Recents::Recents (Options options) : impl (std::make_unique<Impl> (std::move (options))) {}
    Recents::~Recents() = default;

    juce::File Recents::defaultFile()
    {
        return fileOptions().getDefaultFile();
    }

    juce::File Recents::file() const { return impl->options.file; }

    bool Recents::load()
    {
        const std::lock_guard<std::mutex> guard (impl->lock);

        if (! impl->options.file.existsAsFile())
        {
            impl->entries.clear();
            return true;
        }

        juce::PropertiesFile file (impl->options.file, fileOptions());

        if (! file.isValidFile())
            return false;

        const auto text = file.getValue (recentsKey);

        if (text.isEmpty())
        {
            impl->entries.clear();
            return true;
        }

        juce::var parsed;

        if (juce::JSON::parse (text, parsed).failed())
            return false;

        return impl->fromVarLocked (parsed);
    }

    bool Recents::save()
    {
        const std::lock_guard<std::mutex> guard (impl->lock);
        return impl->saveLocked();
    }

    bool Recents::touch (Entry entry)
    {
        if (entry.lastSeenMs == 0)
            entry.lastSeenMs = juce::Time::currentTimeMillis();

        const std::lock_guard<std::mutex> guard (impl->lock);

        impl->entries.erase (std::remove_if (impl->entries.begin(), impl->entries.end(),
                                             [&] (const Entry& e) { return e.sameDevice (entry); }),
                             impl->entries.end());

        impl->entries.insert (impl->entries.begin(), std::move (entry));
        impl->trim();
        return impl->saveLocked();
    }

    bool Recents::remove (const std::string& name, const std::optional<std::string>& identityValue)
    {
        const std::lock_guard<std::mutex> guard (impl->lock);

        const auto before = impl->entries.size();

        impl->entries.erase (std::remove_if (impl->entries.begin(), impl->entries.end(),
                                             [&] (const Entry& e)
                                             {
                                                 return e.name == name
                                                     && (! identityValue.has_value() || e.identityValue == identityValue);
                                             }),
                             impl->entries.end());

        if (impl->entries.size() == before)
            return false;

        impl->saveLocked();
        return true;
    }

    std::vector<Recents::Entry> Recents::all() const
    {
        const std::lock_guard<std::mutex> guard (impl->lock);
        return impl->entries;
    }

    std::optional<Recents::Entry> Recents::find (const std::string& name,
                                                 const std::optional<std::string>& identityValue) const
    {
        const std::lock_guard<std::mutex> guard (impl->lock);

        for (const auto& entry : impl->entries)
            if (entry.name == name && (! identityValue.has_value() || entry.identityValue == identityValue))
                return entry;

        return std::nullopt;
    }

    std::size_t Recents::size() const
    {
        const std::lock_guard<std::mutex> guard (impl->lock);
        return impl->entries.size();
    }

    juce::var Recents::toVar() const
    {
        const std::lock_guard<std::mutex> guard (impl->lock);
        return impl->toVarLocked();
    }

    bool Recents::fromVar (const juce::var& list)
    {
        const std::lock_guard<std::mutex> guard (impl->lock);
        return impl->fromVarLocked (list);
    }
}
