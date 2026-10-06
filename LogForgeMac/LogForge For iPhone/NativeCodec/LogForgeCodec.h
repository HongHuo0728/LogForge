#pragma once
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <stdint.h>
CF_IMPLICIT_BRIDGING_ENABLED
typedef struct LFEncoder LFEncoder;
LFEncoder * _Nullable LFEncoderCreate(int32_t width, int32_t height, int32_t profile);
void LFEncoderDestroy(LFEncoder * _Nullable encoder);
CMSampleBufferRef _Nullable LFEncoderEncode(LFEncoder * _Nonnull encoder, CVPixelBufferRef _Nonnull pixels,
                                           CMTime pts, CMTime duration) CF_RETURNS_RETAINED;
const char * _Nonnull LFEncoderLastError(LFEncoder * _Nullable encoder);
const char * _Nonnull LFCodecVersion(void);

typedef struct LFDecoder LFDecoder;
LFDecoder * _Nullable LFDecoderCreate(int32_t width, int32_t height);
void LFDecoderDestroy(LFDecoder * _Nullable decoder);
int32_t LFDecoderDecode(LFDecoder * _Nonnull decoder, CMSampleBufferRef _Nonnull packet,
                        CVPixelBufferRef _Nonnull pixels);
const char * _Nonnull LFDecoderLastError(LFDecoder * _Nullable decoder);

CF_IMPLICIT_BRIDGING_DISABLED
