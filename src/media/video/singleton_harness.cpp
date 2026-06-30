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

#include "HalProvider.h"   // hal::HalProvider::sharedVideo() (Slice 1b)
#include "IVideo.h"
#include "minimp4.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>

#define TRACE(...) do { printf("[HARNESS] " __VA_ARGS__); fflush(stdout); } while (0)

/* MP4 mux helpers — copied from VideoRecorder.cpp to replicate its MP4 record path. */
static int harnessWriteCb(int64_t offset, const void *buffer, size_t size, void *token) {
    FILE *f = (FILE *)token;
    fseek(f, offset, SEEK_SET);
    return fwrite(buffer, 1, size, f) != size;
}
static ssize_t harnessGetNALSize(uint8_t *buf, ssize_t size) {
    ssize_t pos = 3;
    while ((size - pos) > 3) {
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1) return pos;
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 0 && buf[pos + 3] == 1) return pos;
        pos++;
    }
    return size;
}

int main(int argc, char **argv) {
    int ivdc = (argc > 1 && atoi(argv[1]) == 0) ? 0 : 1;       // argv[1]=0 disables IVDC
    int rec_thumb = (argc > 2 && atoi(argv[2]) == 0) ? 0 : 1;  // argv[2]=0 disables record-time CH2 thumb
    int rec_delay = (argc > 3) ? atoi(argv[3]) : 0;            // argv[3]=seconds to sleep between H264 start and first poll (mimics wm record startup: getInfo/fopen/MP4E/captureThumbnail)
    int rec_mp4 = (argc > 4 && atoi(argv[4]) == 0) ? 0 : 1;    // argv[4]=0 disables MP4 muxing (the last untested wm-record behavior)
    int photo_file = (argc > 5 && atoi(argv[5]) == 1) ? 1 : 0; // argv[5]=1 writes photo .jpg + thumb to files (mimic ImageSnap persistence)
    int bg_upload  = (argc > 6 && atoi(argv[6]) == 1) ? 1 : 0;  // argv[6]=1 spawns bg thread reading the .jpg during record (mimic upload worker concurrent I/O)
    TRACE("=== singleton_harness: media::sharedVideo() + IVideoStream, ivdc=%d rec_thumb=%d rec_delay=%d rec_mp4=%d photo_file=%d bg_upload=%d ===\n", ivdc, rec_thumb, rec_delay, rec_mp4, photo_file, bg_upload);

    TRACE("--> hal::HalProvider::sharedVideo()  [singleton IngenicVideo init]\n");
    auto video = hal::HalProvider::sharedVideo();
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
            if (mainS->polling(1000) && mainS->getFrame(f)) {
                if (photo_file && i == 0) {
                    FILE* jfp = fopen("/mnt/sdcard/DCIM/harness_photo.jpg", "wb");
                    if (jfp) { for (int p = 0; p < f.piece_count; p++) fwrite(f.pieces[p].data, 1, f.pieces[p].size, jfp); fclose(jfp); TRACE("photo: wrote harness_photo.jpg (%d pieces)\n", (int)f.piece_count); }
                }
                mainS->releaseFrame(f);
            }
        }
        TRACE("--> thumb polling/getFrame\n");
        if (thumbS->polling(1000) && thumbS->getFrame(f)) {
            if (photo_file) {
                FILE* tfp = fopen("/mnt/sdcard/DCIM/harness_thumb.jpg", "wb");
                if (tfp) { for (int p = 0; p < f.piece_count; p++) fwrite(f.pieces[p].data, 1, f.pieces[p].size, tfp); fclose(tfp); TRACE("photo: wrote harness_thumb.jpg\n"); }
            }
            thumbS->releaseFrame(f);
        }

        TRACE("--> stop main + thumb\n");
        mainS->stop();
        thumbS->stop();
        TRACE("--> ~stream (ref-counted release — the suspected VPU-poison step)\n");
    }  // ~IngenicVideoStream for mainS + thumbS (ref-counted DisableChn/UnBind/DestroyGroup)
    TRACE("<<< PHOTO done; now RECORD\n");

    /* bg upload-mimic thread: reads harness_photo.jpg repeatedly during the record,
     * mimicking the wm upload worker's concurrent file/network I/O (a cm==1 corruption
     * suspect — HTC_NO_UPLOAD moved the wedge deeper). Only spawned if bg_upload=1. */
    std::atomic<int> bgRun{1};
    std::thread bgTh;
    if (bg_upload) {
        TRACE("--> spawn bg upload-mimic thread (reads harness_photo.jpg ~20x/s during record)\n");
        bgTh = std::thread([&bgRun]() {
            char buf[4096];
            while (bgRun.load()) {
                FILE* fp = fopen("/mnt/sdcard/DCIM/harness_photo.jpg", "rb");
                if (fp) { while (fread(buf, 1, sizeof(buf), fp) > 0) {} fclose(fp); }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            TRACE("<-- bg upload-mimic thread exiting\n");
        });
    }

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

        /* Mimic the wm VideoRecorder::record startup delay between H264 StartRecvPic and
         * the first poll (getInfo/fopen/MP4E_open/mp4_h26x_write_init/captureThumbnail take
         * seconds). During this delay the H264 encoder produces frames with NO consumer.
         * The harness/reproducer poll immediately (no delay) and are clean; cm==2 has the
         * same delay but works — so this tests whether the delay AFTER the photo overflows
         * the encoder buffer and wedges. */
        if (rec_delay > 0) {
            TRACE("--> sleep %ds (mimics wm record startup: H264 running, no consumer)\n", rec_delay);
            sleep(rec_delay);
            TRACE("<-- delay done; now poll\n");
        }

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

        /* MP4 mux setup (mirrors VideoRecorder::record) — the last untested wm-record behavior. */
        FILE *mp4Fp = NULL;
        MP4E_mux_t *muxer = NULL;
        mp4_h26x_writer_t mp4wr;
        if (rec_mp4) {
            TRACE("--> fopen + MP4E_open + mp4_h26x_write_init (MP4 mux, mirrors VideoRecorder::record)\n");
            mp4Fp = fopen("/mnt/sdcard/DCIM/harness.mp4", "wb");
            if (mp4Fp) {
                muxer = MP4E_open(0, 1, mp4Fp, harnessWriteCb);
                if (muxer && mp4_h26x_write_init(&mp4wr, muxer, 2560, 1440, 0) == MP4E_STATUS_OK) {
                    TRACE("<-- MP4 mux ready\n");
                } else {
                    TRACE("MP4E_open/write_init failed; continuing without mux\n");
                    if (muxer) MP4E_close(muxer);
                    muxer = NULL;
                }
            }
        }

        TRACE("--> polling/getFrame loop (SUSTAINED 900 frames via IVideoStream%s)\n", muxer ? " + MP4 mux" : "");
        hal::VideoEncodedFrame f;
        for (int i = 0; i < 900; i++) {
            if ((i % 50) == 0) TRACE("--> poll %d\n", i);
            if (!recS->polling(1000)) { TRACE("poll timeout at %d\n", i); break; }
            if (recS->getFrame(f)) {
                if (muxer && f.piece_count > 0) {
                    /* mux piece 0's NALs (H264 frame) — mirrors VideoRecorder::record */
                    uint8_t *data = (uint8_t *)f.pieces[0].data;
                    ssize_t datasize = (ssize_t)f.pieces[0].size, pos = 0;
                    while (pos < datasize) {
                        ssize_t nal_size = harnessGetNALSize(data + pos, datasize - pos);
                        if (nal_size < 4) { pos += 1; continue; }
                        mp4_h26x_write_nal(&mp4wr, data + pos, (int)nal_size, 3000);  /* 3000 = 90000/30 */
                        pos += nal_size;
                    }
                }
                recS->releaseFrame(f);
            }
        }
        recS->stop();

        if (muxer) {
            TRACE("--> mp4_h26x_write_close + MP4E_close + fclose\n");
            mp4_h26x_write_close(&mp4wr);
            MP4E_close(muxer);
            if (mp4Fp) fclose(mp4Fp);
        }
    }
    bgRun.store(0);
    if (bgTh.joinable()) bgTh.join();
    TRACE("========== HARNESS DONE ==========\n");
    return 0;
}
