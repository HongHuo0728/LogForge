#!/bin/bash
# Pin the SDK that enables Duo's edge-to-edge layout and vertical system bars.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/logs
export DEVELOPER_DIR=/Applications/Xcode_27.1.app/Contents/Developer
test -d "$DEVELOPER_DIR"
if [ -n "${GITHUB_ENV:-}" ]; then echo "DEVELOPER_DIR=$DEVELOPER_DIR" >> "$GITHUB_ENV"; fi
xcodebuild -version | tee build/logs/xcode-version.txt
grep -qx 'Xcode 27.1' build/logs/xcode-version.txt
grep -qx 'Build version 27A9275' build/logs/xcode-version.txt
test "$(xcrun --sdk iphoneos --show-sdk-version)" = 27.1
if ! xcrun --sdk iphoneos metal --version > build/logs/metal-version.txt 2>&1; then
    xcodebuild -downloadComponent metalToolchain 2>&1 | tee build/logs/metal-install.log
    xcrun --sdk iphoneos metal --version > build/logs/metal-version.txt
fi
versions=(27.1)
if [ "${LOGFORGE_COMPAT_SUITE:-release-smoke}" = full ]; then versions+=(26.5); fi
for version in "${versions[@]}"; do
    if ! xcrun simctl list runtimes --json | python3 -c \
        'import json,sys; v=sys.argv[1]; sys.exit(not any(r["version"]==v and r.get("isAvailable") for r in json.load(sys.stdin)["runtimes"]))' "$version"; then
        xcodebuild -downloadPlatform iOS -buildVersion "$version" -architectureVariant arm64 \
            2>&1 | tee "build/logs/runtime-$version-install.log"
    fi
done
xcrun simctl list --json > build/logs/simulator-inventory.json
python3 tools/select-simulator.py duo > build/logs/duo-device.txt
if [ "${LOGFORGE_COMPAT_SUITE:-release-smoke}" = full ]; then
    python3 tools/select-simulator.py phone26 > build/logs/phone26-device.txt
    python3 tools/select-simulator.py ipad > build/logs/ipad-device.txt
fi
