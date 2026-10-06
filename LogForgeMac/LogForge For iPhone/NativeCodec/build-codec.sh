#!/bin/bash
# Called by the Xcode target, independent of any GitHub workflow.
set -euo pipefail
version=8.1.2
sha=464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c
: "${PLATFORM_NAME:?Run as an Xcode build phase}" "${ARCHS:?}" "${TARGET_TEMP_DIR:?}" "${BUILT_PRODUCTS_DIR:?}"
cache="${LOGFORGE_SOURCE_CACHE:-${HOME}/Library/Caches/LogForge}"
mkdir -p "$cache"
archive="$cache/ffmpeg-$version.tar.xz"
if ! test -f "$archive"; then
    temporary=$(mktemp "$cache/ffmpeg-download.XXXXXX")
    trap 'rm -f "$temporary"' EXIT
    curl --fail --location --proto '=https' --tlsv1.2 "https://ffmpeg.org/releases/ffmpeg-$version.tar.xz" -o "$temporary"
    test "$(shasum -a 256 "$temporary" | awk '{print $1}')" = "$sha"
    mv "$temporary" "$archive"
fi
test "$(shasum -a 256 "$archive" | awk '{print $1}')" = "$sha"
sdk=$(xcrun --sdk "$PLATFORM_NAME" --show-sdk-path)
sdkversion=$(xcrun --sdk "$PLATFORM_NAME" --show-sdk-version)
libraries=()
# FFmpeg's generated config.sh cannot safely represent a prefix containing spaces.
# Xcode target directories contain the app name; use a per-target hash under /tmp.
build_key=$(printf '%s' "$TARGET_TEMP_DIR" | shasum -a 256 | awk '{print $1}')
for arch in $ARCHS; do
    prefix="/tmp/LogForgeCodec-$UID/$build_key/decoder-v1-$version-$PLATFORM_NAME-$sdkversion-$arch"
    if ! test -f "$prefix/lib/libLogForgeFFmpeg.a"; then
        mkdir -p "$prefix/source"
        tar -xf "$archive" -C "$prefix/source" --strip-components=1
        target="$arch-apple-ios26.0"
        if test "$PLATFORM_NAME" = iphonesimulator; then target="$target-simulator"; fi
        cpu=armv8; if test "$arch" = x86_64; then cpu=x86_64; fi
        (
            cd "$prefix/source"
            ./configure --prefix="$prefix" --target-os=darwin --arch="$arch" --cpu="$cpu" --enable-cross-compile \
                --cc="$(xcrun --sdk "$PLATFORM_NAME" --find clang)" --sysroot="$sdk" \
                --extra-cflags="-target $target -fPIC" --extra-ldflags="-target $target" \
                --disable-gpl --disable-nonfree --disable-autodetect --disable-everything --disable-programs --disable-doc --disable-debug \
                --disable-asm --disable-shared --enable-static --enable-pic --enable-pthreads \
                --disable-avformat --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample \
                --disable-network --disable-videotoolbox --disable-audiotoolbox \
                --enable-avcodec --enable-avutil --enable-encoder=prores_ks --enable-decoder=prores
            make -j "$(sysctl -n hw.logicalcpu)"
            make install
        )
        xcrun libtool -static "$prefix/lib/libavcodec.a" "$prefix/lib/libavutil.a" -o "$prefix/lib/libLogForgeFFmpeg.a"
    fi
    libraries+=("$prefix/lib/libLogForgeFFmpeg.a")
    mkdir -p "$BUILT_PRODUCTS_DIR/LogForgeCodec/include"
    cp -R "$prefix/include/" "$BUILT_PRODUCTS_DIR/LogForgeCodec/include/"
done
mkdir -p "$BUILT_PRODUCTS_DIR/LogForgeCodec/lib"
xcrun lipo -create "${libraries[@]}" -output "$BUILT_PRODUCTS_DIR/LogForgeCodec/lib/libLogForgeFFmpeg.a"
