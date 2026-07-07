// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <cstdint>

#include "tsduck/tsTOT.h"

namespace aribcap_dump::test {

// Makes an ISDB TOT for a Unix timestamp in milliseconds.
inline ts::TOT MakeTot(std::int64_t unix_ms) {
    return ts::TOT((ts::Time::UnixEpoch + std::chrono::milliseconds(unix_ms)).UTCToJST());
}

}  // namespace aribcap_dump::test
