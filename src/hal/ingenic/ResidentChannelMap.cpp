// T28/T29 — implementation of capToChannelSet (see ResidentChannelMap.h).
//
// T29 revision: um_live no longer unconditionally maps to CH1. The mapping is
// capability-SET driven (design um-capability-advertising §3.6, 2026-07-12
// revision). Single-stream preview ({um_live} alone) now uses CH0 (official
// nrvbs path — kernel pre-allocates CH0 physical memory at boot; using CH1
// alone left CH0's reservation idle but allocated = 64MB OOM root cause).

#include "ResidentChannelMap.h"

namespace hal {

std::vector<int> capToChannelSet(const std::set<std::string>& caps, bool withThumb) {
    std::vector<int> v;
    if (caps.empty()) return v;

    const bool hasLive = caps.count("um_live") > 0;
    const bool hasSnap = caps.count("um_snap") > 0;
    const bool hasRec  = caps.count("um_rec")  > 0;
    // um_pb → no channel (reads SD/sqlite); presence only matters for HTTP/mDNS.

    // um_rec → CH0 (record full-resolution main stream).
    if (hasRec) v.push_back(kChannelRecord);

    // um_live → preview stream. Which channel depends on whether um_rec is
    // also present:
    //   um_rec present → CH1 (sub-stream 720p, classic main/sub pair)
    //   um_rec absent  → CH0 (single-stream 720p via Scaler; official nrvbs
    //                    path, avoids idle CH0 reservation = OOM fix).
    if (hasLive) {
        if (hasRec) v.push_back(kChannelLive);    // CH1 sub-stream
        else        v.push_back(kChannelRecord);  // CH0 single-stream
    }

    // um_snap → CH12 (JPEG photo, shares group0 with CH0).
    if (hasSnap) v.push_back(kChannelPhoto);

    // CH14 thumbnail shared: snap || rec (design §3.6 footnote).
    if (withThumb && (hasSnap || hasRec)) v.push_back(kChannelThumb);

    return v;
}

} // namespace hal
