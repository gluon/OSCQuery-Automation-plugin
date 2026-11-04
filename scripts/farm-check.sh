#!/usr/bin/env bash
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
#
# The CI job's checks on a farm machine (macOS or Linux), for when a runner is slow or a
# branch is not pushed yet: configure `dev`, build Debug, ctest under C and fr_FR, build
# the Release plugin, pluginval 1.0.4 at strictness 5. Logs go next to the checkout.
#
#   Usage: scripts/farm-check.sh <checkout>
#
# Moving an unpushed branch there, as go.dot does:
#   git bundle create oqa.bundle main && scp oqa.bundle macmini:/tmp/
#   ssh macmini 'git clone -b main /tmp/oqa.bundle oqa && cd oqa && git submodule update --init --recursive'
set -uo pipefail
export PATH=/opt/homebrew/bin:$PATH
cd "$1" || exit 1
OS=$(uname -s)
step() { echo; echo "=== $1"; }

step "configure (dev)"
cmake --preset dev > ../farm-oqa-config.log 2>&1 || { tail -30 ../farm-oqa-config.log; exit 1; }
grep '^CMake Warning' ../farm-oqa-config.log | grep -v ThirdParty/ && echo "!! configure warnings outside ThirdParty"

step "build Debug"
cmake --build --preset dev-debug > ../farm-oqa-build.log 2>&1 || { grep -E "error|Error" ../farm-oqa-build.log | head -30; exit 1; }
echo "warnings in our code:"; grep -E "warning:" ../farm-oqa-build.log | grep -v ThirdParty/ | sort -u | head -20; echo "(end)"

step "ctest"
ctest --preset dev-debug || exit 1
./build/dev/tests/Debug/oqa_tests --oqa-locale=fr_FR.UTF-8 2>&1 | tail -3

step "build Release plugin"
cmake --build --preset dev-release --target oqa_plugin_32_All > ../farm-oqa-release.log 2>&1 || { grep -E "error|Error" ../farm-oqa-release.log | head -30; exit 1; }
ART=build/dev/src/shims/juce/oqa_plugin_32_artefacts/Release
ls "$ART"
VST="$ART/VST3/OSCQuery Automation 32.vst3"

step "pluginval 1.0.4, strictness 5"
rm -rf ../pluginval && mkdir ../pluginval
if [ "$OS" = Darwin ]; then
  curl -sSL -o ../pluginval/pv.zip https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_macOS.zip
  unzip -q ../pluginval/pv.zip -d ../pluginval
  PV=../pluginval/pluginval.app/Contents/MacOS/pluginval
  lipo -archs "$VST/Contents/MacOS/OSCQuery Automation 32"
else
  curl -sSL -o ../pluginval/pv.zip https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Linux.zip
  unzip -q ../pluginval/pv.zip -d ../pluginval
  PV=../pluginval/pluginval; chmod +x $PV
fi
EXTRA=""
if [ "$OS" != Darwin ] && [ -z "${DISPLAY:-}" ]; then
  if command -v xvfb-run >/dev/null; then PVRUN="xvfb-run -a"; else PVRUN=""; EXTRA="--skip-gui-tests"; echo "(no DISPLAY and no xvfb: --skip-gui-tests)"; fi
fi
${PVRUN:-} $PV --strictness-level 5 --validate-in-process --timeout-ms 300000 $EXTRA --validate "$VST" > ../farm-oqa-pluginval.log 2>&1
RC=$?
grep -E "FAILED|SUCCESS|!!!" ../farm-oqa-pluginval.log | tail -10
echo "pluginval exit=$RC"
exit $RC
