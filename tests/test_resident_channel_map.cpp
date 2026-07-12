// T28/T29 unit test — um_* capability → resident IMP channel mapping.
//
// capToChannelSet is the pure decision function extracted from
// buildResidentChannels (IngenicVideo.cpp file-local static) into
// ResidentChannelMap.h so the mapping is unit-testable without dragging in
// the IMP stub (sim imp_stub is all no-op — actual channel CONSTRUCTION is a
// device verification item, marked T28-hw-imp-channel-build-unverified).
//
// T29 revision (design um-capability-advertising §3.6, 2026-07-12 official
// nrvbs guide): the mapping is now capability-SET driven. um_live alone maps
// to CH0 (single-stream, Scaler→720p), NOT CH1. The old T28 mapping (um_live
// → CH1 unconditionally) caused the 64MB OOM: CH0's kernel reservation
// (nrvbs, ~11×8MB virtual + 24MB rmem) stayed allocated but idle.
//
// What is locked (R1-R6):
//   R1  {um_live}                    → {CH0}       (single-stream preview, Scaler→720p)
//   R2  {um_live, um_snap}           → {CH0,CH12,CH14} (CH0 preview + photo + thumb)
//   R3  {um_live, um_rec}            → {CH0,CH1,CH14}  (record main + preview sub + thumb)
//   R4  {um_rec}                     → {CH0,CH14}      (CH14 shared: snap || rec)
//   R5  {um_pb}                      → {} empty        (pb reads SD/sqlite, no channel)
//   R6  {um_live, um_snap, um_rec}   → {CH0,CH1,CH12,CH14} (all channels)
//
// Plus: CH14 presence = snap || rec (design §3.6 footnote, NOT &&).
//       withThumb=false suppresses CH14 even when snap/rec present.
//       um_live WITHOUT um_rec always maps to CH0 (single-stream path).

#include "../src/hal/ingenic/ResidentChannelMap.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void requireEq(const std::vector<int>& actual, const std::vector<int>& expected,
               const std::string& label) {
    std::vector<int> a = actual;
    std::vector<int> e = expected;
    std::sort(a.begin(), a.end());
    std::sort(e.begin(), e.end());
    if (a != e) {
        std::cerr << "FAIL [" << label << "]: got {";
        for (size_t i = 0; i < a.size(); ++i) { if (i) std::cerr << ","; std::cerr << a[i]; }
        std::cerr << "}, expected {";
        for (size_t i = 0; i < e.size(); ++i) { if (i) std::cerr << ","; std::cerr << e[i]; }
        std::cerr << "}" << std::endl;
        ++g_failures;
    }
}

}  // namespace

int main() {
    using namespace hal;

    // R1 — um_live only → CH0 (single-stream preview via Scaler→720p).
    // T29: was CH1 (T28); revised to CH0 per official nrvbs guide (design §3.6).
    requireEq(capToChannelSet({"um_live"}, true),
              {kChannelRecord},
              "R1 {um_live} -> {CH0} (single-stream)");

    // R2 — um_live + um_snap → CH0 + CH12 + CH14 (preview + photo + thumb)
    // CH0 single-stream (um_rec absent) + CH12 photo shares group0 + CH14 thumb.
    requireEq(capToChannelSet({"um_live", "um_snap"}, true),
              {kChannelRecord, kChannelPhoto, kChannelThumb},
              "R2 {um_live,um_snap} -> {CH0,CH12,CH14}");

    // R3 — um_live + um_rec → CH0 + CH1 + CH14 (record main + preview sub + thumb)
    // um_rec present → CH0 = record full-res; um_live → CH1 (sub-stream 720p).
    requireEq(capToChannelSet({"um_live", "um_rec"}, true),
              {kChannelRecord, kChannelLive, kChannelThumb},
              "R3 {um_live,um_rec} -> {CH0,CH1,CH14}");

    // R4 — um_rec alone → CH0 + CH14 (record + thumb; CH14 shared: snap || rec)
    requireEq(capToChannelSet({"um_rec"}, true),
              {kChannelRecord, kChannelThumb},
              "R4 {um_rec} -> {CH0,CH14} (thumb shared)");

    // R5 — um_pb alone → {} (no IMP channel, reads SD/sqlite)
    requireEq(capToChannelSet({"um_pb"}, true),
              {},
              "R5 {um_pb} -> {} (no channel)");

    // R6 — all three media caps → CH0 + CH1 + CH12 + CH14
    requireEq(capToChannelSet({"um_live", "um_snap", "um_rec"}, true),
              {kChannelRecord, kChannelLive, kChannelPhoto, kChannelThumb},
              "R6 {um_live,um_snap,um_rec} -> {CH0,CH1,CH12,CH14}");

    // Extra — all four caps (um_pb adds no channel)
    requireEq(capToChannelSet({"um_live", "um_snap", "um_rec", "um_pb"}, true),
              {kChannelRecord, kChannelPhoto, kChannelLive, kChannelThumb},
              "all caps -> {CH0,CH12,CH1,CH14}");

    // Extra — withThumb=false suppresses CH14 even when snap+rec present
    requireEq(capToChannelSet({"um_snap", "um_rec"}, false),
              {kChannelRecord, kChannelPhoto},
              "withThumb=false -> no CH14");

    // Extra — empty caps → empty (buildResidentChannels treats this as "not
    // cap-driven" and falls through to residentMode path; capToChannelSet
    // itself returns empty to signal "no cap-driven channels")
    requireEq(capToChannelSet({}, true),
              {},
              "empty caps -> {} (fallthrough signal)");

    // Extra — CH14 NOT built for um_live-only (footnote: snap || rec)
    {
        auto v = capToChannelSet({"um_live"}, true);
        if (std::find(v.begin(), v.end(), kChannelThumb) != v.end()) {
            std::cerr << "FAIL: CH14 built for um_live-only (should need snap||rec)" << std::endl;
            ++g_failures;
        }
    }

    // Extra — um_live WITHOUT um_rec always maps to CH0 (never CH1)
    // This is the T29 core invariant: single-stream = CH0 (official nrvbs path).
    {
        auto v = capToChannelSet({"um_live"}, true);
        if (std::find(v.begin(), v.end(), kChannelLive) != v.end()) {
            std::cerr << "FAIL: CH1 built for um_live-only (should be CH0 single-stream)" << std::endl;
            ++g_failures;
        }
        if (std::find(v.begin(), v.end(), kChannelRecord) == v.end()) {
            std::cerr << "FAIL: CH0 missing for um_live-only (single-stream requires CH0)" << std::endl;
            ++g_failures;
        }
    }

    if (g_failures != 0) {
        std::cerr << "FAIL: " << g_failures << " assertion(s) failed" << std::endl;
        return 1;
    }
    std::cout << "OK: test_resident_channel_map R1-R6 + edge cases green" << std::endl;
    return 0;
}
