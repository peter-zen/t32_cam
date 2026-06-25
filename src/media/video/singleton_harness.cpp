// singleton_harness.cpp — standalone test of the SharedVideo singleton +
// IngenicVideoStream C++ object path (the prime suspect for the wm cm==1 wedge).
//
// This mirrors the sample reproducer's `seq-g0-wm` (group-0 JPEG main + group-2 JPEG
// thumb, then H264 on group 0) but via the wm's OWN IVideoStream C++ object — i.e. the
// singleton + the ref-counted release in ~IngenicVideoStream — NOT the raw IMP calls the
// sample reproducer uses. The reproducer (raw IMP) was CLEAN across 7 variants; the wm
// (singleton) WEDGES. This harness isolates the C++ object / singleton as the variable.
//
// If this wedges at the record's first poll → the IngenicVideo C++ object / singleton
// (not the scheduler, not the bare IMP flow) is the cause.
//
// Build: linked into media_recorder's CMake as `singleton_harness`.
// Run:   /mnt/huntcam/bin/singleton_harness [ivdc]   (ivdc=0 disables IVDC; default 1)

#include "SharedVideo.h"
#include "IVideo.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#define TRACE(...) do { printf("[HARNESS] " __VA_ARGS__); fflush(stdout); } while (0)

int main(int argc, char **argv) {
    int ivdc = (argc > 1 && atoi(argv[1]) == 0) ? 0 : 1;       // argv[1]=0 disables IVDC
    int rec_thumb = (argc > 2 && atoi(argv[2]) == 0) ? 0 : 1;  // argv[2]=0 disables record-time CH2 thumb
    TRACE("=== singleton_harness: media::sharedVideo() + IVideoStream, ivdc=%d rec_thumb=%d ===\n", ivdc, rec_thumb);

    TRACE("--> media::sharedVideo()  [singleton IngenicVideo init]\n");
    auto video = media::sharedVideo();
    if (!video) { TRACE("sharedVideo FAILED\n"); return 1; }
    TRACE("<-- sharedVideo OK (singleton init'd)\n");

    /* ---------------- PHOTO: JPEG main (group 0) + JPEG thumb (group 2), concurrent ---------------- */
    TRACE(">>> PHOTO via singleton IVideoStream (mirrors seq-g0-wm)\n");
    {
        hal::VideoStreamConfig mainCfg{
            hal::VideoPayloadType::JPEG, hal::VideoChannelId{0, 0}, 2560, 1440, 15, 1, 40, 0, 0, 0,
            hal::VideoRcMode::FIXQP, 3, 4, ivdc ? true : false};
        hal::VideoStreamConfig thumbCfg{
            hal::VideoPayloadType::JPEG, hal::VideoChannelId{0, 2}, 320, 180, 15, 1, 60, 0, 0, 0,
            hal::VideoRcMode::FIXQP, 3, 4, ivdc ? true : false};

        TRACE("--> createVideoStream(main) + configure JPEG g0 2560x1440\n");
        auto mainS = video->createVideoStream();
        if (!mainS || !mainS->configure(mainCfg)) { TRACE("main configure FAILED\n"); return 1; }

        TRACE("--> createVideoStream(thumb) + configure JPEG g2 320x180\n");
        auto thumbS = video->createVideoStream();
        if (!thumbS || !thumbS->configure(thumbCfg)) { TRACE("thumb configure FAILED\n"); return 1; }

        TRACE("--> start main + thumb\n");
        mainS->start();
        thumbS->start();

        hal::VideoEncodedFrame f;
        TRACE("--> main polling/getFrame x3\n");
        for (int i = 0; i < 3; i++) {
            if (mainS->polling(1000) && mainS->getFrame(f)) mainS->releaseFrame(f);
        }
        TRACE("--> thumb polling/getFrame\n");
        if (thumbS->polling(1000) && thumbS->getFrame(f)) thumbS->releaseFrame(f);

        TRACE("--> stop main + thumb\n");
        mainS->stop();
        thumbS->stop();
        TRACE("--> ~stream (ref-counted release — the suspected VPU-poison step)\n");
    }  // ~IngenicVideoStream for mainS + thumbS (ref-counted DisableChn/UnBind/DestroyGroup)
    TRACE("<<< PHOTO done; now RECORD\n");

    /* ---------------- RECORD: H264 (group 0) — WEDGE POINT is the first poll ---------------- */
    TRACE(">>> RECORD via singleton IVideoStream H264 g0 2560x1440 30fps\n");
    {
        hal::VideoStreamConfig h264Cfg{
            hal::VideoPayloadType::H264, hal::VideoChannelId{0, 0}, 2560, 1440, 30, 1, 0, 4096, 0, 60,
            hal::VideoRcMode::CBR, 3, 4, ivdc ? true : false};

        TRACE("--> createVideoStream + configure H264 g0\n");
        auto recS = video->createVideoStream();
        if (!recS || !recS->configure(h264Cfg)) { TRACE("H264 configure FAILED\n"); return 1; }

        TRACE("--> start H264\n");
        recS->start();

        /* captureThumbnail: a brief CH2 JPEG grab right after H264 start, mirroring
         * VideoRecorder::record's concurrentSnap captureThumbnail (the leading suspect —
         * the harness record WITHOUT this was CLEAN). If this wedges, the record-time
         * CH2 thumbnail (CH2 JPEG concurrent with the just-started H264) is the trigger. */
        if (rec_thumb) {
            TRACE("--> captureThumbnail: CH2 JPEG grab (mirrors VideoRecorder::record)\n");
            hal::VideoStreamConfig thumbCfg{
                hal::VideoPayloadType::JPEG, hal::VideoChannelId{0, 2}, 320, 180, 15, 1, 60, 0, 0, 0,
                hal::VideoRcMode::FIXQP, 3, 4, ivdc ? true : false};
            auto thumbS = video->createVideoStream();
            if (thumbS && thumbS->configure(thumbCfg)) {
                thumbS->start();
                hal::VideoEncodedFrame tf;
                if (thumbS->polling(1000) && thumbS->getFrame(tf)) thumbS->releaseFrame(tf);
                thumbS->stop();
                TRACE("<-- captureThumbnail done; ~thumbS (ref-counted release)\n");
            }
        }

        TRACE("--> polling/getFrame loop (FIRST POLL = WEDGE POINT)\n");
        hal::VideoEncodedFrame f;
        for (int i = 0; i < 10; i++) {
            TRACE("--> poll %d\n", i);
            if (!recS->polling(1000)) { TRACE("poll timeout\n"); break; }
            if (recS->getFrame(f)) recS->releaseFrame(f);
        }
        recS->stop();
    }
    TRACE("========== HARNESS DONE ==========\n");
    return 0;
}
