#!/bin/bash
# Run on macOS with Xcode 26 or newer. No signing secrets are needed.
set -euo pipefail
cd "$(dirname "$0")/.."
root="$PWD"
export LOGFORGE_SOURCE_CACHE="${LOGFORGE_SOURCE_CACHE:-${RUNNER_TEMP:-$root/build}/LogForgeSource}"
project="$root/LogForge For iPhone/LogForge For iPhone.xcodeproj"
scheme='LogForge For iPhone'
release_identity=$(python3 tools/release-info.py)
read -r version release_build <<< "$release_identity"
bundle_build="${GITHUB_RUN_NUMBER:-1}"
ipa_name="LogForge-${version}-iOS-${release_build}-unsigned.ipa"
relink_name="LogForge-${version}-iOS-${release_build}-RelinkKit.zip"
mkdir -p build/logs build/artifacts
xcodebuild -version | tee build/logs/xcode-version.txt
sdk_version=$(xcrun --sdk iphoneos --show-sdk-version)
if [ "${sdk_version%%.*}" -lt 26 ]; then
    echo "An iOS 26 or newer SDK is required, found $sdk_version" >&2
    exit 1
fi
common=(-project "$project" -scheme "$scheme" -derivedDataPath "$root/build/DerivedData"
    "MARKETING_VERSION=$version" "LOGFORGE_RELEASE_BUILD=$release_build"
    "CURRENT_PROJECT_VERSION=$bundle_build"
    CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO 'CODE_SIGN_IDENTITY=' 'DEVELOPMENT_TEAM=')
case "${1:-archive}" in
archive)
    xcodebuild "${common[@]}" -configuration Release -destination 'generic/platform=iOS' \
        -archivePath "$root/build/LogForge.xcarchive" -resultBundlePath "$root/build/Archive.xcresult" \
        archive 2>&1 | tee build/logs/archive.log
    app="$root/build/LogForge.xcarchive/Products/Applications/$scheme.app"
    test -d "$app"
    python3 tools/release-info.py --app-info "$app/Info.plist" --bundle-build "$bundle_build" \
        --manifest build/artifacts/release-info.json
    mkdir -p build/package/Payload
    ditto "$app" "build/package/Payload/$scheme.app"
    (cd build/package && ditto -c -k --keepParent Payload "../artifacts/$ipa_name")
    # Keep app objects and FFmpeg source available for LGPL relinking.
    target_temp=$(find "$root/build/DerivedData/Build/Intermediates.noindex/ArchiveIntermediates" \
        -type d -name "$scheme.build" -path '*/Release-iphoneos/*' | head -n 1)
    codec=$(find "$root/build/DerivedData" -type d -name LogForgeCodec -path '*/BuildProductsPath/*' | head -n 1)
    test -n "$target_temp" && test -n "$codec"
    bash 'LogForge For iPhone/NativeCodec/package-relink.sh' "$root/LogForge For iPhone" \
        "$target_temp" "$(dirname "$codec")" "$root/build/RelinkKit"
    cp build/logs/archive.log build/logs/xcode-version.txt build/RelinkKit/
    ditto -c -k --keepParent build/RelinkKit "build/artifacts/$relink_name"
    (cd build/artifacts && shasum -a 256 "$ipa_name" > "$ipa_name.sha256" \
        && shasum -a 256 "$relink_name" > "$relink_name.sha256")
    cp build/logs/xcode-version.txt build/artifacts/
    cat > build/artifacts/READ-ME.txt <<EOF
LogForge $version ($release_build), Apple bundle build $bundle_build.
$ipa_name is a real iPhone (arm64) build, without an Apple signature.
It cannot be installed by tapping the file. Sign it using your own valid Apple
development/distribution identity and an appropriate provisioning profile.
The included RelinkKit contains FFmpeg source, static library, app objects and
the original build log. Keep it with the app when distributing a signed build.

LogForge $version ($release_build)，Apple 内部构建号 $bundle_build。
IPA 为未签名的 iPhone 正式配置构建，需要使用自己的有效 Apple 签名和配置文件安装。
分发签名后的应用时，请同时提供对应的 RelinkKit、源码和许可声明。
主项目：https://github.com/HongHuo0728/LogForge
EOF
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        printf '### iPhone build ready\nVersion: **%s (%s)** · Apple build: **%s**\n\nDownload `%s` and the RelinkKit from this run’s Artifacts section.\n\nThe IPA needs signing before installation. No Apple certificate was used.\n' "$version" "$release_build" "$bundle_build" "$ipa_name" >> "$GITHUB_STEP_SUMMARY"
    fi
    ;;
test)
    xcrun simctl list devices available --json > build/logs/simulators.json
    simulator_info=$(python3 - <<'PY'
import json
with open('build/logs/simulators.json') as f:
    devices = json.load(f)['devices']
for runtime, items in sorted(devices.items(), reverse=True):
    if '.iOS-' not in runtime:
        continue
    version = int(runtime.split('.iOS-')[-1].split('-')[0])
    if version < 26:
        continue
    for item in items:
        if item['name'].startswith('iPhone') and item.get('isAvailable', False):
            print(item['udid'], item['state'])
            raise SystemExit(0)
raise SystemExit('No available iPhone simulator with iOS 26 or newer')
PY
    )
    read -r simulator simulator_state <<< "$simulator_info"
    # Start booting while Xcode compiles, avoiding a serial cold-boot delay.
    if [ "$simulator_state" != Booted ]; then
        xcrun simctl boot "$simulator" 2>&1 | tee build/logs/simulator-boot.log
    fi
    xcodebuild "${common[@]}" -configuration Debug -destination "platform=iOS Simulator,id=$simulator" \
        -parallel-testing-enabled NO -only-testing:'LogForge For iPhoneTests' \
        -test-timeouts-enabled YES -default-test-execution-time-allowance 120 \
        -maximum-test-execution-time-allowance 180 \
        -resultBundlePath "$root/build/Tests.xcresult" test 2>&1 | tee build/logs/tests.log
    ;;
*) echo "Usage: $0 archive|test" >&2; exit 2 ;;
esac
