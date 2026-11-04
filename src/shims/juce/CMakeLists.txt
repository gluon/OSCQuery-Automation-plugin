# This file is part of the OSCQuery Automation Plugin —
# https://github.com/pob31/OSCQuery-Automation-plugin
#
# Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle
#
# The OSCQuery Automation Plugin is free software: you can redistribute it
# and/or modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation, either version 3 of the License,
# or (at your option) any later version. It is distributed in the hope that it
# will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
# Public License (LICENSE, at the repository root) for more details.
#
# SPDX-License-Identifier: GPL-3.0-or-later

# The JUCE shim: one AudioProcessor, built as VST3 everywhere and AU on macOS.
#
# PLUGIN IDENTITY (decided 2026-11-04). The VST3 class ID and the AU type, subtype and
# manufacturer are derived from the manufacturer code, the plugin code and
# JUCE_VST3_CAN_REPLACE_VST2 (cmake/OqaThirdParty.cmake). Once a session has saved the
# plugin, none of the three may change, or the session no longer finds it.
#
#   Company "Pix et Bel", manufacturer code PxBl, as WFS-DIY.
#   Plugin codes Q008 Q016 Q032 Q064 Q128 for the slot-count variants, QMon for the
#   Monitor. Phase 0 builds Q032 only; Phase 7 adds the other four, one line each.

set(OQA_PLUGIN_COMPANY       "Pix et Bel")
set(OQA_PLUGIN_MANUFACTURER  "PxBl")
set(OQA_PLUGIN_BUNDLE_PREFIX "io.github.pob31.oqa")

set(OQA_PLUGIN_FORMATS VST3)
if(APPLE)
    list(APPEND OQA_PLUGIN_FORMATS AU)
endif()

# One slot-count variant: target oqa_plugin_<slots>, product "OSCQuery Automation <slots>".
function(oqa_add_plugin_variant slots code)
    set(target oqa_plugin_${slots})

    juce_add_plugin(${target}
        COMPANY_NAME                "${OQA_PLUGIN_COMPANY}"
        PLUGIN_MANUFACTURER_CODE    "${OQA_PLUGIN_MANUFACTURER}"
        PLUGIN_CODE                 "${code}"
        BUNDLE_ID                   "${OQA_PLUGIN_BUNDLE_PREFIX}.${code}"
        PRODUCT_NAME                "OSCQuery Automation ${slots}"
        DESCRIPTION                 "Automation lanes bound to OSCQuery parameters"
        VERSION                     "${PROJECT_VERSION}"
        FORMATS                     ${OQA_PLUGIN_FORMATS}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE
        VST3_CATEGORIES             "Fx" "Network"
        AU_MAIN_TYPE                "kAudioUnitType_Effect")

    target_sources(${target} PRIVATE
        PluginProcessor.h
        PluginProcessor.cpp)

    target_compile_definitions(${target} PRIVATE
        OQA_SLOT_COUNT=${slots})

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_processors
            oqa::juce_config
            oqa::warnings
            juce::juce_recommended_config_flags)
endfunction()

oqa_add_plugin_variant(32 Q032)
