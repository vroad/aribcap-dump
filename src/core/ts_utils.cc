// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/ts_utils.hpp"

#include "tscore/tsMemory.h"
#include "tsduck/tsTS.h"

namespace aribcap_dump {

std::int64_t Pts90kToMs(std::uint64_t pts) {
    return cn::duration_cast<cn::milliseconds>(ts::PTS(pts & ts::PTS_DTS_MASK)).count();
}

std::optional<std::uint64_t> ParsePesPts(const ts::PESPacket& packet) {
    if (!packet.hasLongHeader() || packet.headerSize() < 14) {
        return std::nullopt;
    }

    const auto* header = packet.header();
    const std::uint8_t pts_dts_flags = header[7] & 0xC0;

    if (pts_dts_flags != 0x80 && pts_dts_flags != 0xC0) {
        return std::nullopt;
    }

    const auto* pts_field = header + 9;

    if ((pts_dts_flags == 0x80 && (pts_field[0] & 0xF1) != 0x21) ||
        (pts_dts_flags == 0xC0 && (pts_field[0] & 0xF1) != 0x31) || (pts_field[2] & 0x01) != 0x01 ||
        (pts_field[4] & 0x01) != 0x01) {
        return std::nullopt;
    }

    return ((static_cast<std::uint64_t>(pts_field[0] & 0x0E) << 29) |
            (static_cast<std::uint64_t>(ts::GetUInt16(pts_field + 1) & 0xFFFE) << 14) |
            (static_cast<std::uint64_t>(ts::GetUInt16(pts_field + 3)) >> 1)) &
           ts::PTS_DTS_MASK;
}

std::optional<std::int64_t> JstTimeToUnixMs(const ts::Time& time) {
    if (time == ts::Time::Epoch) {
        return std::nullopt;
    }

    const auto utc = time.JSTToUTC();

    if (utc < ts::Time::UnixEpoch) {
        return std::nullopt;
    }

    return (utc - ts::Time::UnixEpoch).count();
}

}  // namespace aribcap_dump
