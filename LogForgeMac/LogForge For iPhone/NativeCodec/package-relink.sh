#!/bin/bash
# Post-build distribution helper, not a GitHub Actions workflow.
# Usage: package-relink.sh <project root> <app target temp directory> <built products directory> <output directory>
set -euo pipefail
test "$#" = 4
project=$(cd "$1" && pwd)
temporary=$(cd "$2" && pwd)
products=$(cd "$3" && pwd)
mkdir -p "$4"
destination=$(cd "$4" && pwd)
mkdir -p "$destination/AppObjects" "$destination/Source" "$destination/Libraries"
cp -R "$temporary/Objects-normal/" "$destination/AppObjects/"
cp -R "$products/LogForgeCodec/" "$destination/Libraries/"
cp -R "$project/NativeCodec" "$destination/Source/"
cp "$project/LogForge For iPhone/SoftwareCodec-LICENSE.txt" "$destination/"
cp "$project/LogForge For iPhone.xcodeproj/project.pbxproj" "$destination/Source/"
cache="${LOGFORGE_SOURCE_CACHE:-${HOME}/Library/Caches/LogForge}"
cp "$cache/ffmpeg-8.1.2.tar.xz" "$destination/Source/"
cat > "$destination/RELINK.txt" <<'EOF'
This kit preserves the application object files, Swift modules, codec library and corresponding codec source.
Use the same Xcode/SDK and architecture as the original build. Rebuild FFmpeg with build-codec.sh after your
changes, replace Libraries/LogForgeCodec/lib/libLogForgeFFmpeg.a, and relink the application object files using
the link invocation from the original Xcode build log. Re-sign the result with your own development identity.
Include that original link invocation and Xcode version with this kit when distributing it; absolute build paths
in the invocation and LinkFileList need to be replaced by paths inside this kit. This helper does not sign or
publish an app, and does not waive any required distribution or installation permissions.
EOF
