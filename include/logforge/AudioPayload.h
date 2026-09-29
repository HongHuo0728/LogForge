#pragma once
#include "Media.h"
namespace logforge {
// Hash each mapped audio stream's concatenated packet payload. FFmpeg streams
// the bytes; the application retains only one SHA-256 digest per audio stream.
Json VerifyAudioPayload(const fs::path& ffmpeg, const fs::path& input, const fs::path& output,
                        size_t streams, const std::atomic_bool& cancel);
}
