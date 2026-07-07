// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>

#include "tscore/tsTime.h"
#include "tsduck/tsPESPacket.h"

namespace aribcap_dump {

[[nodiscard]] std::int64_t Pts90kToMs(std::uint64_t pts);
[[nodiscard]] std::optional<std::uint64_t> ParsePesPts(const ts::PESPacket& packet);
[[nodiscard]] std::optional<std::int64_t> JstTimeToUnixMs(const ts::Time& time);

}  // namespace aribcap_dump
