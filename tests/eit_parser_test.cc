// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/eit_parser.hpp"

#include <cstdint>
#include <memory>
#include <vector>

#include "tests/eit_test_utils.hpp"
#include "tscore/tsTime.h"
#include "tsduck/tsDID.h"
#include "tsduck/tsDescriptor.h"
#include "tsduck/tsDuckContext.h"
#include "tsduck/tsEIT.h"
#include "tsduck/tsExtendedEventDescriptor.h"
#include "tsduck/tsShortEventDescriptor.h"

#include <catch2/catch_test_macros.hpp>

namespace {

using aribcap_dump::test::MakePfEit;

struct EitParserFixture {
    ts::DuckContext context;
    ts::EIT eit = MakePfEit(1024, 0, 12345, 32736);

    // Appends an event to the fixture's EIT and returns it for descriptor customization.
    ts::EIT::Event& AddEvent(std::uint16_t event_id, const ts::Time& start_time,
                             cn::seconds duration) {
        auto& event = eit.events.newEntry();
        event.event_id = event_id;
        event.start_time = start_time;
        event.duration = duration;

        return event;
    }

    // Runs the fixture's EIT through `ParseEit`.
    std::vector<aribcap_dump::EitRecord> Parse(bool present_section_has_event = true) {
        return aribcap_dump::ParseEit(context,
                                      aribcap_dump::DeserializedEit{
                                          .eit = eit,
                                          .version = eit.version(),
                                          .present_section_has_event = present_section_has_event,
                                      });
    }
};

}  // namespace

// -------------------------------------------------------------------------------------------------
// Event count and validity guard tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(EitParserFixture, "ParseEit returns no results for an EIT with no events") {
    CHECK(Parse().empty());
}

TEST_CASE_METHOD(EitParserFixture, "ParseEit returns no results for an invalidated EIT") {
    AddEvent(0x1234, ts::Time(2020, 1, 1, 0, 0, 0), cn::seconds(60));
    eit.invalidate();

    CHECK(Parse().empty());
}

TEST_CASE_METHOD(EitParserFixture, "ParseEit caps output at the first two events") {
    AddEvent(0x0001, ts::Time(2020, 1, 1, 0, 0, 0), cn::seconds(60));
    AddEvent(0x0002, ts::Time(2020, 1, 1, 1, 0, 0), cn::seconds(60));
    AddEvent(0x0003, ts::Time(2020, 1, 1, 2, 0, 0), cn::seconds(60));

    const auto results = Parse();

    REQUIRE(results.size() == 2);
    CHECK(results[0].event_id == 0x0001);
    CHECK(results[1].event_id == 0x0002);
}

// -------------------------------------------------------------------------------------------------
// Per-event field mapping tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(EitParserFixture, "ParseEit maps present/following and converts JST start times") {
    AddEvent(0x1234, ts::Time(2020, 1, 1, 0, 0, 0), cn::seconds(90 * 60));
    AddEvent(0x1235, ts::Time(2020, 1, 1, 1, 30, 0), cn::seconds(90 * 60));

    const auto results = Parse();

    REQUIRE(results.size() == 2);

    // start_time_ms literals below are the same JST wall-clock times passed to AddEvent above,
    // just converted to UTC epoch-ms (JST is UTC+9).
    CHECK(results[0].version == 0);
    CHECK(results[0].service_id == 1024);
    CHECK(results[0].transport_stream_id == 12345);
    CHECK(results[0].original_network_id == 32736);
    CHECK(results[0].event_id == 0x1234);
    CHECK(results[0].section == aribcap_dump::EitSection::kPresent);
    CHECK(results[0].start_time_ms == 1'577'804'400'000LL);
    CHECK(results[0].duration_sec == 90U * 60U);

    CHECK(results[1].version == 0);
    CHECK(results[1].event_id == 0x1235);
    CHECK(results[1].section == aribcap_dump::EitSection::kFollowing);
    CHECK(results[1].start_time_ms == 1'577'809'800'000LL);
    CHECK(results[1].duration_sec == 90U * 60U);
}

TEST_CASE_METHOD(EitParserFixture,
                 "ParseEit labels the only event as following when the present section is empty") {
    AddEvent(0x1235, ts::Time(2020, 1, 1, 1, 30, 0), cn::seconds(90 * 60));

    const auto results = Parse(/*present_section_has_event=*/false);

    REQUIRE(results.size() == 1);
    CHECK(results[0].event_id == 0x1235);
    CHECK(results[0].section == aribcap_dump::EitSection::kFollowing);
}

TEST_CASE_METHOD(EitParserFixture, "ParseEit drops out-of-range durations") {
    // Valid range is [0, UINT32_MAX] seconds; these are one below and one above it.
    AddEvent(0x0001, ts::Time(2020, 1, 1, 0, 0, 0), cn::seconds(-1));
    AddEvent(0x0002, ts::Time(2020, 1, 1, 1, 0, 0), cn::seconds(std::int64_t{0x1'0000'0000}));

    const auto results = Parse();

    REQUIRE(results.size() == 2);
    CHECK_FALSE(results[0].duration_sec.has_value());
    CHECK_FALSE(results[1].duration_sec.has_value());
}

// -------------------------------------------------------------------------------------------------
// short_event_descriptor tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(EitParserFixture,
                 "ParseEit keeps multiple short event descriptors as distinct list entries") {
    auto& event = AddEvent(0x1234, ts::Time(2020, 1, 1, 0, 0, 0), cn::seconds(60));
    event.descs.add(context, ts::ShortEventDescriptor(u"eng", u"first", u"one"));
    event.descs.add(context, ts::ShortEventDescriptor(u"jpn", u"second", u"two"));

    const auto results = Parse();

    REQUIRE(results.size() == 1);
    REQUIRE(results[0].short_events.size() == 2);
    CHECK(results[0].short_events[0].language_code == "eng");
    CHECK(results[0].short_events[0].event_name == "first");
    CHECK(results[0].short_events[0].text == "one");
    CHECK(results[0].short_events[1].language_code == "jpn");
    CHECK(results[0].short_events[1].event_name == "second");
    CHECK(results[0].short_events[1].text == "two");
}

TEST_CASE_METHOD(EitParserFixture,
                 "ParseEit skips a malformed descriptor and keeps the valid ones") {
    auto& event = AddEvent(0x1234, ts::Time(2020, 1, 1, 0, 0, 0), cn::seconds(60));

    // The correct tag (DID_DVB_SHORT_EVENT) lets ParseDescriptors find it, but the 0-byte
    // payload is too short for ShortEventDescriptor::deserialize — the malformed descriptor
    // this test targets.
    event.descs.add(std::make_shared<ts::Descriptor>(ts::DID_DVB_SHORT_EVENT, nullptr, 0));
    event.descs.add(context, ts::ShortEventDescriptor(u"eng", u"name", u"text"));

    const auto results = Parse();

    REQUIRE(results.size() == 1);
    REQUIRE(results[0].short_events.size() == 1);
    CHECK(results[0].short_events[0].event_name == "name");
}

// -------------------------------------------------------------------------------------------------
// extended_event_descriptor text joining tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(EitParserFixture, "ParseEit joins extended event items text and trailing text") {
    auto& event = AddEvent(0x1234, ts::Time(2020, 1, 1, 0, 0, 0), cn::seconds(60));
    event.descs.add(context, ts::ShortEventDescriptor(u"eng", u"name", u"short text"));

    ts::ExtendedEventDescriptor extended;
    extended.language_code = u"eng";
    extended.entries.emplace_back(u"item", u"value");
    extended.text = u"tail";
    event.descs.add(context, extended);

    const auto results = Parse();

    REQUIRE(results.size() == 1);
    REQUIRE(results[0].short_events.size() == 1);
    CHECK(results[0].short_events[0].event_name == "name");
    CHECK(results[0].extended_text == "item\nvalue\ntail");
}

TEST_CASE_METHOD(EitParserFixture, "ParseEit merges extended event item continuations") {
    auto& event = AddEvent(0x1234, ts::Time(2020, 1, 1, 0, 0, 0), cn::seconds(60));

    ts::ExtendedEventDescriptor extended;
    extended.language_code = u"eng";
    extended.entries.emplace_back(u"key", u"first ");
    // empty item_description below (u"") marks this entry as a continuation of the previous
    // entry's item value ("first "); ARIB STD-B10 splits values this way when they
    // exceed a single descriptor's size limit. PushParsedExtendedEventItem should merge
    // them into one value.
    extended.entries.emplace_back(u"", u"second");
    event.descs.add(context, extended);

    const auto results = Parse();

    REQUIRE(results.size() == 1);
    CHECK(results[0].extended_text == "key\nfirst second");
}

TEST_CASE_METHOD(EitParserFixture, "ParseEit renders an orphan continuation item as bare value") {
    auto& event = AddEvent(0x1234, ts::Time(2020, 1, 1, 0, 0, 0), cn::seconds(60));

    // An empty item_description (u""") with no preceding item (e.g. the first entry of the
    // first extended_event_descriptor) has nothing to continue; it is kept as its own
    // unnamed item instead.
    ts::ExtendedEventDescriptor extended;
    extended.language_code = u"eng";
    extended.entries.emplace_back(u"", u"orphan");
    event.descs.add(context, extended);

    const auto results = Parse();

    REQUIRE(results.size() == 1);
    CHECK(results[0].extended_text == "orphan");
}

TEST_CASE_METHOD(
    EitParserFixture,
    "ParseEit joins extended event items text and trailing text split across descriptor "
    "instances") {
    auto& event = AddEvent(0x1234, ts::Time(2020, 1, 1, 0, 0, 0), cn::seconds(60));

    // ARIB STD-B10 splits one logical extended event across multiple
    // extended_event_descriptor instances (descriptor_number/last_descriptor_number)
    // when it exceeds a single descriptor's size limit. The item text and trailing text
    // both accumulate across the two descriptor instances (`first` and `second` below)
    // instead of restarting per descriptor.
    ts::ExtendedEventDescriptor first;
    first.descriptor_number = 0;
    first.last_descriptor_number = 1;
    first.language_code = u"eng";
    first.entries.emplace_back(u"key", u"first ");
    first.text = u"tail1 ";
    event.descs.add(context, first);

    ts::ExtendedEventDescriptor second;
    second.descriptor_number = 1;
    second.last_descriptor_number = 1;
    second.language_code = u"eng";
    second.entries.emplace_back(u"", u"second");
    second.text = u"tail2";
    event.descs.add(context, second);

    const auto results = Parse();

    REQUIRE(results.size() == 1);
    CHECK(results[0].extended_text == "key\nfirst second\ntail1 tail2");
}
