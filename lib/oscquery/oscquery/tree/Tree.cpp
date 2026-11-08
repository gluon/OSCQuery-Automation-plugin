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

#include <oscquery/tree/Tree.h>

#include <oscquery/osc/Address.h>

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace oqa::oscquery
{
    //==============================================================================
    //  Paths
    namespace
    {
        std::string_view withoutTrailingSlash (std::string_view path)
        {
            while (path.size() > 1 && path.back() == '/')
                path.remove_suffix (1);

            return path;
        }

        std::string joinPath (const std::string& parent, const std::string& segment)
        {
            if (parent.empty() || parent == "/")
                return "/" + segment;

            return parent + "/" + segment;
        }

        /*  Deeper than this is not a device tree. JUCE's JSON parser has its
            own limit; this one is the model's, and it is what keeps a hostile
            reply from unwinding the stack through the recursion below. */
        constexpr int maxDepth = 128;
    }

    std::string parentPathOf (std::string_view path)
    {
        path = withoutTrailingSlash (path);

        if (path.empty() || path == "/")
            return "";

        const auto slash = path.rfind ('/');

        if (slash == std::string_view::npos)
            return "";

        return slash == 0 ? std::string ("/") : std::string (path.substr (0, slash));
    }

    std::string lastSegmentOf (std::string_view path)
    {
        path = withoutTrailingSlash (path);

        if (path.empty() || path == "/")
            return "";

        const auto slash = path.rfind ('/');
        return std::string (slash == std::string_view::npos ? path : path.substr (slash + 1));
    }

    //==============================================================================
    //  Range, Node
    bool Range::operator== (const Range& other) const
    {
        return min == other.min && max == other.max && vals == other.vals;
    }

    Argument Node::argument (std::size_t index) const
    {
        Argument out;

        if (! type.has_value() || index >= type->size())
            return out;

        out.tag = (*type)[index];

        if (index < range.size())
            out.range = range[index];

        if (hasValue && index < value.size())
            out.value = value[index];

        if (index < unit.size())
            out.unit = unit[index];

        if (index < clipmode.size())
            out.clipmode = clipmode[index];

        return out;
    }

    std::string Node::name() const
    {
        return lastSegmentOf (fullPath);
    }

    //==============================================================================
    //  Values
    namespace
    {
        bool isNumber (const juce::var& v) noexcept
        {
            return v.isInt() || v.isInt64() || v.isDouble();
        }

        /** A number read as a flag: anything but zero is true. */
        bool nonZero (const juce::var& v) noexcept
        {
            if (v.isInt() || v.isInt64())
                return static_cast<juce::int64> (v) != 0;

            return std::fpclassify (static_cast<double> (v)) != FP_ZERO;
        }

        /*  juce::var answers isObject() for an array as well, and the
            DynamicObject behind an array is null: a JSON object is one that
            is not an array. */
        bool isJsonObject (const juce::var& v) noexcept
        {
            return v.isObject() && ! v.isArray() && v.getDynamicObject() != nullptr;
        }

        osc::Value fallbackValue (const juce::var& v)
        {
            if (v.isBool())
                return osc::Value::boolean (static_cast<bool> (v));

            if (isNumber (v))
                return osc::Value::float64 (static_cast<double> (v));

            if (v.isString())
                return osc::Value::string (v.toString().toStdString());

            return osc::Value::nil();
        }
    }

    osc::Value valueFromVar (const juce::var& v, char tag)
    {
        switch (tag)
        {
            case 'i':
                if (isNumber (v)) return osc::Value::int32 (static_cast<std::int32_t> (static_cast<juce::int64> (v)));
                if (v.isBool())   return osc::Value::int32 (static_cast<bool> (v) ? 1 : 0);
                break;

            case 'h':
                if (isNumber (v)) return osc::Value::int64 (static_cast<std::int64_t> (static_cast<juce::int64> (v)));
                break;

            case 'f':
                if (isNumber (v)) return osc::Value::float32 (static_cast<float> (static_cast<double> (v)));
                if (v.isBool())   return osc::Value::float32 (static_cast<bool> (v) ? 1.0f : 0.0f);
                break;

            case 'd':
                if (isNumber (v)) return osc::Value::float64 (static_cast<double> (v));
                break;

            case 's':
                if (v.isString() || isNumber (v) || v.isBool())
                    return osc::Value::string (v.toString().toStdString());
                break;

            case 'T':
            case 'F':
                if (v.isBool())   return osc::Value::boolean (static_cast<bool> (v));
                if (isNumber (v)) return osc::Value::boolean (nonZero (v));
                if (v.isString()) return osc::Value::boolean (v.toString().equalsIgnoreCase ("true"));
                break;

            case 'N':
                return osc::Value::nil();

            case 'I':
                return osc::Value::impulse();

            case 't':
                if (isNumber (v))
                    return osc::Value::timeTag (osc::TimeTag { static_cast<std::uint64_t> (static_cast<juce::int64> (v)) });
                break;

            case 'b':
                if (v.isString())
                {
                    juce::MemoryOutputStream decoded;

                    if (juce::Base64::convertFromBase64 (decoded, v.toString()))
                    {
                        osc::Blob blob;
                        const auto* bytes = static_cast<const std::uint8_t*> (decoded.getData());
                        blob.bytes.assign (bytes, bytes + decoded.getDataSize());
                        return osc::Value::blob (std::move (blob));
                    }
                }
                break;

            default:
                break;
        }

        return fallbackValue (v);
    }

    osc::Values valuesFromVar (const juce::var& v, const std::string& type)
    {
        osc::Values out;

        if (v.isVoid() || v.isUndefined())
            return out;

        if (const auto* array = v.getArray())
        {
            out.reserve (static_cast<std::size_t> (array->size()));

            for (int i = 0; i < array->size(); ++i)
            {
                const auto index = static_cast<std::size_t> (i);
                out.push_back (valueFromVar (array->getReference (i), index < type.size() ? type[index] : '\0'));
            }

            return out;
        }

        out.push_back (valueFromVar (v, type.empty() ? '\0' : type[0]));
        return out;
    }

    juce::var varFromValue (const osc::Value& value)
    {
        switch (value.type())
        {
            case osc::Value::Type::int32:     return juce::var (value.getInt32());
            case osc::Value::Type::int64:     return juce::var (static_cast<juce::int64> (value.getInt64()));
            case osc::Value::Type::float64:   return juce::var (value.getFloat64());
            case osc::Value::Type::string:    return juce::var (juce::String (value.getString()));
            case osc::Value::Type::boolTrue:  return juce::var (true);
            case osc::Value::Type::boolFalse: return juce::var (false);
            case osc::Value::Type::timeTag:   return juce::var (static_cast<juce::int64> (value.getTimeTag().raw));

            case osc::Value::Type::float32:
            {
                /*  Written from the float, not from its double promotion: 0.1f
                    promoted is 0.10000000149011612, which is true and useless
                    in a JSON a human reads. The shortest text identifying the
                    float, read back as a double, is a double that narrows to
                    the same float. */
                const auto back = osc::parseDouble (osc::formatFloat (value.getFloat32()));
                return juce::var (back.value_or (static_cast<double> (value.getFloat32())));
            }

            case osc::Value::Type::blob:
            {
                const auto& bytes = value.getBlob().bytes;
                return juce::var (juce::Base64::toBase64 (bytes.data(), bytes.size()));
            }

            case osc::Value::Type::nil:
            case osc::Value::Type::impulse:
            default:
                return juce::var();
        }
    }

    //==============================================================================
    //  Parsing
    namespace
    {
        std::vector<std::string> stringList (const juce::var& v)
        {
            std::vector<std::string> out;

            if (const auto* array = v.getArray())
            {
                for (const auto& item : *array)
                    out.push_back (item.isVoid() ? std::string() : item.toString().toStdString());
            }
            else if (v.isString())
            {
                out.push_back (v.toString().toStdString());
            }

            return out;
        }

        std::optional<Range> parseRange (const juce::var& entry, char tag)
        {
            if (! isJsonObject (entry))
                return std::nullopt;

            Range range;

            const auto min = entry.getProperty ("MIN", {});
            const auto max = entry.getProperty ("MAX", {});

            if (isNumber (min)) range.min = static_cast<double> (min);
            if (isNumber (max)) range.max = static_cast<double> (max);

            if (const auto* vals = entry.getProperty ("VALS", {}).getArray())
                for (const auto& item : *vals)
                    range.vals.push_back (valueFromVar (item, tag));

            if (range.empty())
                return std::nullopt;

            return range;
        }

        std::optional<Node> parseNodeAt (const juce::var& object, const std::string& pathIfAbsent,
                                         int depth, std::string& error)
        {
            if (! isJsonObject (object))
            {
                error = "the node at " + pathIfAbsent + " is not a JSON object";
                return std::nullopt;
            }

            if (depth > maxDepth)
            {
                error = "the tree is nested deeper than " + std::to_string (maxDepth) + " levels at " + pathIfAbsent;
                return std::nullopt;
            }

            Node node;
            juce::var rangeVar, valueVar, contentsVar;
            bool hasContents = false;

            for (const auto& property : object.getDynamicObject()->getProperties())
            {
                const auto key = property.name.toString();
                const auto& v = property.value;

                if (key == "FULL_PATH")        { if (v.isString()) node.fullPath = v.toString().toStdString(); }
                else if (key == "TYPE")        { if (v.isString()) node.type = v.toString().toStdString(); }
                else if (key == "ACCESS")      { if (isNumber (v)) node.access = static_cast<int> (v); }
                else if (key == "DESCRIPTION") { if (v.isString()) node.description = v.toString().toStdString(); }
                else if (key == "CRITICAL")    { node.critical = v.isBool() ? static_cast<bool> (v) : (isNumber (v) && nonZero (v)); }
                else if (key == "RANGE")       { rangeVar = v; }
                else if (key == "VALUE")       { valueVar = v; }
                else if (key == "UNIT")        { node.unit = stringList (v); }
                else if (key == "CLIPMODE")    { node.clipmode = stringList (v); }
                else if (key == "CONTENTS")    { contentsVar = v; hasContents = true; }
                else                           { node.extras.set (property.name, v); }
            }

            if (node.fullPath.empty())
                node.fullPath = pathIfAbsent;

            const std::string type = node.type.value_or (std::string());

            //  RANGE: an array with one entry per argument; a bare object is one entry.
            if (const auto* entries = rangeVar.getArray())
            {
                for (int i = 0; i < entries->size(); ++i)
                {
                    const auto index = static_cast<std::size_t> (i);
                    node.range.push_back (parseRange (entries->getReference (i), index < type.size() ? type[index] : '\0'));
                }
            }
            else if (isJsonObject (rangeVar))
            {
                node.range.push_back (parseRange (rangeVar, type.empty() ? '\0' : type[0]));
            }

            if (! valueVar.isVoid())
            {
                node.hasValue = true;
                node.value = valuesFromVar (valueVar, type);
            }

            /*  A container's CONTENTS. Absent on a container, the children were
                not sent - the device answered shallowly, or this is a node a
                later subtree GET fills in - and `loaded` says so. A leaf is
                loaded by definition. */
            if (hasContents && isJsonObject (contentsVar))
            {
                node.loaded = true;

                for (const auto& child : contentsVar.getDynamicObject()->getProperties())
                {
                    const auto key = child.name.toString().toStdString();
                    auto parsed = parseNodeAt (child.value, joinPath (node.fullPath, key), depth + 1, error);

                    if (! parsed.has_value())
                        return std::nullopt;

                    auto segment = lastSegmentOf (parsed->fullPath);

                    if (segment.empty())
                        segment = key;

                    node.children[segment] = std::move (*parsed);
                }
            }
            else
            {
                node.loaded = node.isLeaf() || (hasContents && ! isJsonObject (contentsVar));
            }

            return node;
        }

        void countNodes (const Node& node, std::size_t& all, std::size_t& leaves)
        {
            ++all;

            if (node.isLeaf())
                ++leaves;

            for (const auto& child : node.children)
                countNodes (child.second, all, leaves);
        }

        void collectLeaves (const Node& node, std::vector<const Node*>& out)
        {
            if (node.isLeaf())
                out.push_back (&node);

            for (const auto& child : node.children)
                collectLeaves (child.second, out);
        }

        void visitAll (const Node& node, const std::function<void (const Node&)>& visit)
        {
            visit (node);

            for (const auto& child : node.children)
                visitAll (child.second, visit);
        }

        void rewritePaths (Node& node, const std::string& newPath)
        {
            node.fullPath = newPath;

            for (auto& child : node.children)
                rewritePaths (child.second, joinPath (newPath, child.first));
        }

        juce::var rangeToVar (const std::optional<Range>& range)
        {
            if (! range.has_value())
                return juce::var();

            auto* object = new juce::DynamicObject();

            if (range->min.has_value()) object->setProperty ("MIN", *range->min);
            if (range->max.has_value()) object->setProperty ("MAX", *range->max);

            if (! range->vals.empty())
            {
                juce::Array<juce::var> vals;

                for (const auto& value : range->vals)
                    vals.add (varFromValue (value));

                object->setProperty ("VALS", vals);
            }

            return juce::var (object);
        }

        juce::var stringsToVar (const std::vector<std::string>& strings)
        {
            juce::Array<juce::var> out;

            for (const auto& s : strings)
                out.add (juce::String (s));

            return out;
        }

        juce::var nodeToVar (const Node& node)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty ("FULL_PATH", juce::String (node.fullPath));

            if (node.type.has_value())
                object->setProperty ("TYPE", juce::String (*node.type));

            if (node.access.has_value())
                object->setProperty ("ACCESS", *node.access);

            if (node.hasValue)
            {
                juce::Array<juce::var> values;

                for (const auto& value : node.value)
                    values.add (varFromValue (value));

                object->setProperty ("VALUE", values);
            }

            if (! node.range.empty())
            {
                juce::Array<juce::var> ranges;

                for (const auto& range : node.range)
                    ranges.add (rangeToVar (range));

                object->setProperty ("RANGE", ranges);
            }

            if (! node.unit.empty())
                object->setProperty ("UNIT", stringsToVar (node.unit));

            if (! node.clipmode.empty())
                object->setProperty ("CLIPMODE", stringsToVar (node.clipmode));

            if (node.description.has_value())
                object->setProperty ("DESCRIPTION", juce::String (*node.description));

            if (node.critical)
                object->setProperty ("CRITICAL", true);

            for (const auto& extra : node.extras)
                object->setProperty (extra.name, extra.value);

            if (node.isContainer() && node.loaded)
            {
                auto* contents = new juce::DynamicObject();

                for (const auto& child : node.children)
                    contents->setProperty (juce::Identifier (juce::String (child.first)), nodeToVar (child.second));

                object->setProperty ("CONTENTS", juce::var (contents));
            }

            return juce::var (object);
        }
    }

    //==============================================================================
    Tree::Tree()
    {
        rootNode.fullPath = "/";
        rootNode.loaded = true;
    }

    std::optional<Node> Tree::parseNode (const juce::var& object, const std::string& pathIfAbsent,
                                         std::string& error)
    {
        return parseNodeAt (object, pathIfAbsent, 0, error);
    }

    std::optional<Tree> Tree::parse (const juce::var& root, std::string& error)
    {
        auto node = parseNodeAt (root, "/", 0, error);

        if (! node.has_value())
            return std::nullopt;

        Tree tree;
        tree.rootNode = std::move (*node);
        return tree;
    }

    std::optional<Tree> Tree::parse (std::string_view json, std::string& error)
    {
        juce::var parsed;
        const auto result = juce::JSON::parse (juce::String (std::string (json)), parsed);

        if (result.failed())
        {
            error = "the tree is not JSON: " + result.getErrorMessage().toStdString();
            return std::nullopt;
        }

        return parse (parsed, error);
    }

    //==============================================================================
    const Node* Tree::find (std::string_view path) const
    {
        const Node* node = &rootNode;

        for (const auto& segment : osc::partsOf (path))
        {
            const auto it = node->children.find (segment);

            if (it == node->children.end())
                return nullptr;

            node = &it->second;
        }

        return node;
    }

    Node* Tree::find (std::string_view path)
    {
        return const_cast<Node*> (static_cast<const Tree&> (*this).find (path));
    }

    bool Tree::merge (Node node)
    {
        const auto path = std::string (withoutTrailingSlash (node.fullPath));

        if (path.empty() || path == "/")
        {
            node.fullPath = "/";
            rootNode = std::move (node);
            return true;
        }

        auto* parent = find (parentPathOf (path));

        if (parent == nullptr)
            return false;

        const auto segment = lastSegmentOf (path);
        parent->children[segment] = std::move (node);
        parent->loaded = true;
        return true;
    }

    bool Tree::merge (const juce::var& subtree, const std::string& pathIfAbsent, std::string& error)
    {
        auto node = parseNodeAt (subtree, pathIfAbsent, 0, error);

        if (! node.has_value())
            return false;

        if (! merge (std::move (*node)))
        {
            error = "no parent for " + pathIfAbsent + " in the tree";
            return false;
        }

        return true;
    }

    bool Tree::remove (std::string_view path)
    {
        const auto parentPath = parentPathOf (path);

        if (parentPath.empty())
            return false;                       // the root, or not a path

        auto* parent = find (parentPath);

        if (parent == nullptr)
            return false;

        return parent->children.erase (lastSegmentOf (path)) > 0;
    }

    bool Tree::rename (std::string_view oldPath, std::string_view newPath)
    {
        const auto from = std::string (withoutTrailingSlash (oldPath));
        const auto to = std::string (withoutTrailingSlash (newPath));

        if (from.empty() || from == "/" || to.empty() || to == "/" || from == to)
            return false;

        //  Into its own subtree: the new parent would be the node being moved.
        if (to.rfind (from + "/", 0) == 0)
            return false;

        if (find (to) != nullptr)
            return false;

        auto* oldParent = find (parentPathOf (from));
        auto* newParent = find (parentPathOf (to));

        if (oldParent == nullptr || newParent == nullptr)
            return false;

        const auto it = oldParent->children.find (lastSegmentOf (from));

        if (it == oldParent->children.end())
            return false;

        Node moved = std::move (it->second);
        oldParent->children.erase (it);

        rewritePaths (moved, to);
        newParent->children[lastSegmentOf (to)] = std::move (moved);
        newParent->loaded = true;
        return true;
    }

    //==============================================================================
    std::size_t Tree::nodeCount() const
    {
        std::size_t all = 0, leaves = 0;
        countNodes (rootNode, all, leaves);
        return all;
    }

    std::size_t Tree::leafCount() const
    {
        std::size_t all = 0, leaves = 0;
        countNodes (rootNode, all, leaves);
        return leaves;
    }

    std::vector<const Node*> Tree::leaves() const
    {
        std::vector<const Node*> out;
        collectLeaves (rootNode, out);

        std::sort (out.begin(), out.end(),
                   [] (const Node* a, const Node* b) { return a->fullPath < b->fullPath; });

        return out;
    }

    void Tree::forEach (const std::function<void (const Node&)>& visit) const
    {
        visitAll (rootNode, visit);
    }

    std::string Tree::treeHash() const
    {
        std::vector<std::string> lines;

        for (const auto* leaf : leaves())
            lines.push_back (leaf->fullPath + "\t" + leaf->type.value_or (std::string()) + "\n");

        //  Byte order, which for UTF-8 is what std::string's operator< gives.
        std::sort (lines.begin(), lines.end());

        std::string text;

        for (const auto& line : lines)
            text += line;

        return juce::SHA256 (text.data(), text.size()).toHexString().toStdString();
    }

    juce::var Tree::toVar() const
    {
        return nodeToVar (rootNode);
    }
}
