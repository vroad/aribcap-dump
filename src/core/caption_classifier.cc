// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/caption_classifier.hpp"

#include "tsduck/tsDID.h"
#include "tsduck/tsDataComponentDescriptor.h"
#include "tsduck/tsStreamType.h"

namespace aribcap_dump {
namespace {

[[nodiscard]] bool IsOneSegPmtPid(ts::PID pid) {
    return pid >= kOneSegPmtPidMin && pid <= kOneSegPmtPidMax;
}

// A missing data_component_id defaults to Profile A, per ARIB STD-B24.
[[nodiscard]] bool IsSupportedProfileAId(std::optional<std::uint16_t> data_component_id) {
    return !data_component_id.has_value() || data_component_id == kDataComponentProfileA;
}

[[nodiscard]] bool IsDefaultCaptionComponentTag(std::uint8_t component_tag) {
    return component_tag == kComponentTagDefaultCaption;
}

// Applies the ARIB caption-pairing rules to an already-extracted
// component_tag/data_component_id/PMT-PID triple.
[[nodiscard]] std::optional<aribcaption::Profile> ClassifyCaptionComponent(
    std::uint8_t component_tag, std::optional<std::uint16_t> data_component_id,
    bool one_seg_pmt_pid) {
    // Profile A caption: valid only with a Profile A (or absent) data_component_id
    // on a non-one-seg PID.
    if (IsDefaultCaptionComponentTag(component_tag)) {
        if (one_seg_pmt_pid || !IsSupportedProfileAId(data_component_id)) {
            return std::nullopt;
        }

        return aribcaption::Profile::kProfileA;
    }

    // Profile C one-seg caption: valid only with a Profile C data_component_id on
    // a one-seg PID.
    if (component_tag == kComponentTagOneSegCaption) {
        if (!one_seg_pmt_pid || data_component_id != kDataComponentProfileC) {
            return std::nullopt;
        }

        return aribcaption::Profile::kProfileC;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint16_t> FindDataComponentId(
    ts::DuckContext& context, const ts::DescriptorList& descriptors) {
    ts::DataComponentDescriptor descriptor;

    if (descriptors.search(context, ts::DID_ISDB_DATA_COMP, descriptor) < descriptors.count()) {
        return descriptor.data_component_id;
    }

    return std::nullopt;
}

}  // namespace

std::optional<aribcaption::Profile> ClassifyCaptionStream(ts::DuckContext& context, ts::PID pmt_pid,
                                                          const ts::PMT::Stream& stream) {
    if (stream.stream_type != ts::ST_PES_PRIV) {
        return std::nullopt;
    }

    std::uint8_t component_tag = 0;

    if (!stream.getComponentTag(component_tag)) {
        return std::nullopt;
    }

    const auto data_component_id = FindDataComponentId(context, stream.descs);

    return ClassifyCaptionComponent(component_tag, data_component_id, IsOneSegPmtPid(pmt_pid));
}

}  // namespace aribcap_dump
