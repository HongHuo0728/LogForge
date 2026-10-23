#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/logs/duo
test -d 'build/DerivedData/Build/Products/Debug-iphonesimulator/LogForge For iPhone.app'
# The Xcode image does not include the host ffmpeg executable. This tool creates
# test media only; it is never linked into or bundled with the iOS application.
if [ "${LOGFORGE_DUO_SUITE:-release-smoke}" = full ]; then
if ! command -v ffmpeg >/dev/null 2>&1; then
    HOMEBREW_NO_AUTO_UPDATE=1 brew install ffmpeg 2>&1 | tee build/logs/duo/ffmpeg-install.log
fi
ffmpeg -version > build/logs/duo/ffmpeg-version.txt
fi
# Capture available Apple controls as diagnostics for unsupported simulator poses.
xcrun devicectl help device settings > build/logs/duo/device-settings-help.txt 2>&1 || true
xcrun simctl help ui > build/logs/duo/simulator-ui-help.txt 2>&1 || true
xcrun simctl help > build/logs/duo/simulator-help.txt 2>&1 || true
xcrun simctl help io > build/logs/duo/simulator-io-help.txt 2>&1 || true
export APPIUM_HOME="${RUNNER_TEMP:-$PWD/build}/logforge-appium-home"
prefix="${RUNNER_TEMP:-$PWD/build}/logforge-appium"
npm install --prefix "$prefix" --no-audit --no-fund appium@3.8.0
appium="$prefix/node_modules/.bin/appium"
"$appium" driver install --source=npm appium-xcuitest-driver@12.19.0
"$appium" --address 127.0.0.1 --port 4723 --log-timestamp \
    > build/logs/duo/appium.log 2>&1 &
server=$!
trap 'kill "$server" 2>/dev/null || true' EXIT
# Real media for the conversion/fold/retry test; no mocked conversion state.
if [ "${LOGFORGE_DUO_SUITE:-release-smoke}" = full ]; then
ffmpeg -hide_banner -nostdin -y -f lavfi -i 'testsrc2=size=1920x1080:rate=30:duration=60' \
    -c:v prores_ks -profile:v 3 -pix_fmt yuv422p10le -color_primaries bt2020 \
    -color_trc arib-std-b67 -colorspace bt2020nc -color_range tv -an \
    "${RUNNER_TEMP:-$PWD/build}/duo-conversion.mov" > build/logs/duo/fixture.log 2>&1
export LOGFORGE_DUO_FIXTURE="${RUNNER_TEMP:-$PWD/build}/duo-conversion.mov"
fi
python3 tools/test_duo.py
