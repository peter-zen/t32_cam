#pragma once
// T28 — pure decision function: map a um_* capability presence-set to the
// resident IMP channel id list. Extracted from buildResidentChannels (which is
// file-local static in IngenicVideo.cpp) so the mapping is unit-testable
// without dragging in the IMP stub.
//
// T29 — mapping revised per design um-capability-advertising §3.6 (2026-07-12
// revision, official Ingenic nrvbs guide). The mapping is now capability-SET
// driven (not single-token): whether um_live maps to CH0 or CH1 depends on
// whether um_rec is also present.
//
// Mapping (design um-capability-advertising §3.6):
//   {um_live}             → CH0  (group0, single-stream preview, Scaler→720p)
//                           Official single-stream path: kernel cmdline nrvbs
//                           pre-allocates physical memory for CH0 at boot, so
//                           single-stream MUST use CH0 — using CH1 alone leaves
//                           the CH0 reservation idle but still allocated (64MB
//                           OOM root cause, T28 post-mortem).
//   {um_live, um_rec}     → CH0 (record full-res) + CH1 (preview 720p sub-stream)
//                           Classic main/sub stream pair.
//   {um_live, um_snap}    → CH0 (preview 720p) + CH12 (JPEG photo, shares group0)
//   um_rec (without live) → CH0 (record full-res) + CH14 thumb
//   um_snap               → CH12 + CH14 if withThumb
//   um_pb                 → no channel (reads SD/sqlite)
// CH14 thumbnail shared: built when snap OR rec is present (not AND).
//
// Returns channel ids only (NOT group/payload/cfg) — the caller (IngenicVideo
// buildResidentChannels) owns the mk() lambda that fills full ResidentChannelDef.
// Sorted ascending for deterministic comparison in tests.
//
// This header has no IMP deps; safe to include from sim tests.

#include <set>
#include <vector>
#include <string>

namespace hal {

// Canonical channel ids (match IngenicVideo.cpp buildResidentChannels).
constexpr int kChannelRecord  = 0;   // group0 H264 — record full-res OR single-stream preview (T29)
constexpr int kChannelLive    = 1;   // group1 H264 1280x720 — preview sub-stream (when um_rec also present)
constexpr int kChannelPhoto   = 12;  // group0 JPEG 2560x1440
constexpr int kChannelThumb   = 14;  // group2 JPEG 320x180

std::vector<int> capToChannelSet(const std::set<std::string>& caps, bool withThumb);

} // namespace hal
