// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>

#include "aribcaption/decoder.hpp"
#include "tsduck/tsDuckContext.h"
#include "tsduck/tsPMT.h"

namespace aribcap_dump {

// ARIB TR-B14 v6.7 Fascicle 2, section 4.2.8.1:
//
// component_tag values for caption/superimpose PES streams in the PMT
// stream_identifier_descriptor.
//
// Layers other than the partial reception layer:
//   caption ES:     0x30..0x37, default caption ES:     0x30
//   superimpose ES: 0x38..0x3F, default superimpose ES: 0x38
//
// Partial reception layer (one-seg):
//   caption ES: 0x87
inline constexpr std::uint8_t kComponentTagCaptionMin = 0x30;
inline constexpr std::uint8_t kComponentTagCaptionMax = 0x37;
inline constexpr std::uint8_t kComponentTagSuperimposeMin = 0x38;
inline constexpr std::uint8_t kComponentTagSuperimposeMax = 0x3F;
inline constexpr std::uint8_t kComponentTagOneSegCaption = 0x87;

// ARIB STD-B10 v5.13 Annex J, Table J-1:
//
// data_component_id values for the Data Component Descriptor (defined in section 6.2.20).
// 0x0008 is "ARIB-Subtitle & teletext coding" (Profile A); 0x0012 is "Subtitle coding for
// digital terrestrial broadcasting (Profile C)".
inline constexpr std::uint16_t kDataComponentProfileA = 0x0008;
inline constexpr std::uint16_t kDataComponentProfileC = 0x0012;

// ARIB TR-B14 v6.0 Fascicle 5, section 5.2.9(2), Table 5-7:
//
// PMT_PID values (0x1FC8-0x1FCF) for partial-reception (one-seg) services, one per lower
// 3 bits of the service identifier. Operational guidelines cap actual usage at 3
// services, with service no. 0 (PMT_PID 0x1FC8) as the primary one.
inline constexpr ts::PID kOneSegPmtPidMin = 0x1FC8;
inline constexpr ts::PID kOneSegPmtPidMax = 0x1FCF;

// Caption type and profile used by libaribcaption to decode one ARIB caption or superimpose stream.
struct CaptionStreamInfo {
    aribcaption::CaptionType caption_type;
    aribcaption::Profile profile;

    [[nodiscard]] bool operator==(const CaptionStreamInfo&) const = default;
};

// Classifies a PMT stream entry as a supported ARIB caption or superimpose stream,
// using the PMT PID and stream descriptors.
//
// Returns the libaribcaption caption type and profile, or std::nullopt when the
// stream is not a supported ARIB caption/superimpose stream.
[[nodiscard]] std::optional<CaptionStreamInfo> ClassifyCaptionStream(ts::DuckContext& context,
                                                                     ts::PID pmt_pid,
                                                                     const ts::PMT::Stream& stream);

}  // namespace aribcap_dump
