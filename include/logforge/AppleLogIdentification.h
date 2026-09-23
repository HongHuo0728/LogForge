#pragma once
#include "Media.h"
#include <string_view>

namespace logforge {
// Container identification only. Call after encoding/rotation and before final
// validation/publication. Never apply to source HLG pixels or a published file.
class AppleLogIdentificationWriter {
  public:
    static constexpr std::string_view Identifier = "com.apple.rec2020.apple-log";
    static void WriteToEncodedPartial(const fs::path& partial, const std::atomic_bool& cancel);
    static Json Validate(const Json& analysis);
};
} // namespace logforge
