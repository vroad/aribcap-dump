// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "tscore/tsTime.h"
#include "tsduck/tsPESPacket.h"

namespace aribcap_dump {

[[nodiscard]] std::int64_t Pts90kToMs(std::uint64_t pts);
[[nodiscard]] std::optional<std::uint64_t> ParsePesPts(const ts::PESPacket& packet);
[[nodiscard]] std::optional<std::int64_t> JstTimeToUnixMs(const ts::Time& time);

// Formats a Unix-epoch millisecond timestamp as an RFC 3339 string in JST,
// e.g. "2020-01-01T00:00:00.000+09:00".
//
// The formatted string always uses 3 fractional-second digits.
// `unix_ms` must be non-negative.
[[nodiscard]] std::string UnixMsToRfc3339Jst(std::int64_t unix_ms);

}  // namespace aribcap_dump
