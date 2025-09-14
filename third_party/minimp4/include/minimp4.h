#ifndef MINIMP4_H
#define MINIMP4_H
/*
    https://github.com/aspt/mp4
    https://github.com/lieff/minimp4
    To the extent possible under law, the author(s) have dedicated all copyright and related and neighboring rights to this software to the public domain worldwide.
    This software is distributed without any warranty.
    See <http://creativecommons.org/publicdomain/zero/1.0/>.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MINIMP4_MIN(x, y) ((x) < (y) ? (x) : (y))

/************************************************************************/
/*                  Build configuration                                 */
/************************************************************************/

#define FIX_BAD_ANDROID_META_BOX  1

#define MAX_CHUNKS_DEPTH          64 // Max chunks nesting level

#define MINIMP4_MAX_SPS 32
#define MINIMP4_MAX_PPS 256

#define MINIMP4_TRANSCODE_SPS_ID  1

// Support indexing of MP4 files over 4 GB.
// If disabled, files with 64-bit offset fields is still supported,
// but error signaled if such field contains too big offset
// This switch affect return type of MP4D_frame_offset() function
#define MINIMP4_ALLOW_64BIT       1

#define MP4D_TRACE_SUPPORTED      0 // Debug trace
#define MP4D_TRACE_TIMESTAMPS     1
// Support parsing of supplementary information, not necessary for decoding:
// duration, language, bitrate, metadata tags, etc
#define MP4D_INFO_SUPPORTED       1

// Enable code, which prints to stdout supplementary MP4 information:
#define MP4D_PRINT_INFO_SUPPORTED 0

#define MP4D_AVC_SUPPORTED        1
#define MP4D_HEVC_SUPPORTED       1
#define MP4D_TIMESTAMPS_SUPPORTED 1

// Enable TrackFragmentBaseMediaDecodeTimeBox support
#define MP4D_TFDT_SUPPORT         0

/************************************************************************/
/*          Some values of MP4(E/D)_track_t->object_type_indication     */
/************************************************************************/
// MPEG-4 AAC (all profiles)
#define MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3                  0x40
// MPEG-2 AAC, Main profile
#define MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_MAIN_PROFILE     0x66
// MPEG-2 AAC, LC profile
#define MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_LC_PROFILE       0x67
// MPEG-2 AAC, SSR profile
#define MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_SSR_PROFILE      0x68
// H.264 (AVC) video
#define MP4_OBJECT_TYPE_AVC                                    0x21
// H.265 (HEVC) video
#define MP4_OBJECT_TYPE_HEVC                                   0x23
// http://www.mp4ra.org/object.html 0xC0-E0  && 0xE2 - 0xFE are specified as "user private"
#define MP4_OBJECT_TYPE_USER_PRIVATE                           0xC0

/************************************************************************/
/*          API error codes                                             */
/************************************************************************/
#define MP4E_STATUS_OK                       0
#define MP4E_STATUS_BAD_ARGUMENTS           -1
#define MP4E_STATUS_NO_MEMORY               -2
#define MP4E_STATUS_FILE_WRITE_ERROR        -3
#define MP4E_STATUS_ONLY_ONE_DSI_ALLOWED    -4

/************************************************************************/
/*          Sample kind for MP4E_put_sample()                           */
/************************************************************************/
#define MP4E_SAMPLE_DEFAULT             0   // (beginning of) audio or video frame
#define MP4E_SAMPLE_RANDOM_ACCESS       1   // mark sample as random access point (key frame)
#define MP4E_SAMPLE_CONTINUATION        2   // Not a sample, but continuation of previous sample (new slice)

/************************************************************************/
/*                  Portable 64-bit type definition                     */
/************************************************************************/

#if MINIMP4_ALLOW_64BIT
    typedef uint64_t boxsize_t;
#else
    typedef unsigned int boxsize_t;
#endif
typedef boxsize_t MP4D_file_offset_t;

/************************************************************************/
/*          Some values of MP4D_track_t->handler_type              */
/************************************************************************/
// Video track : 'vide'
#define MP4D_HANDLER_TYPE_VIDE                                  0x76696465
// Audio track : 'soun'
#define MP4D_HANDLER_TYPE_SOUN                                  0x736F756E
// General MPEG-4 systems streams (without specific handler).
// Used for private stream, as suggested in http://www.mp4ra.org/handler.html
#define MP4E_HANDLER_TYPE_GESM                                  0x6765736D


#define HEVC_NAL_VPS 32
#define HEVC_NAL_SPS 33
#define HEVC_NAL_PPS 34
#define HEVC_NAL_BLA_W_LP 16
#define HEVC_NAL_CRA_NUT  21

/************************************************************************/
/*          Data structures                                             */
/************************************************************************/

typedef struct MP4E_mux_tag MP4E_mux_t;

typedef enum
{
    e_audio,
    e_video,
    e_private
} track_media_kind_t;

typedef struct
{
    // MP4 object type code, which defined codec class for the track.
    // See MP4E_OBJECT_TYPE_* values for some codecs
    unsigned object_type_indication;

    // Track language: 3-char ISO 639-2T code: "und", "eng", "rus", "jpn" etc...
    unsigned char language[4];

    track_media_kind_t track_media_kind;

    // 90000 for video, sample rate for audio
    unsigned time_scale;
    unsigned default_duration;

    union
    {
        struct
        {
            // number of channels in the audio track.
            unsigned channelcount;
        } a;

        struct
        {
            int width;
            int height;
        } v;
    } u;

} MP4E_track_t;

typedef struct MP4D_sample_to_chunk_t_tag MP4D_sample_to_chunk_t;

typedef struct
{
    /************************************************************************/
    /*                 mandatory public data                                */
    /************************************************************************/
    // How many 'samples' in the track
    // The 'sample' is MP4 term, denoting audio or video frame
    unsigned sample_count;

    // Decoder-specific info (DSI) data
    unsigned char *dsi;

    // DSI data size
    unsigned dsi_bytes;

    // MP4 object type code
    // case 0x00: return "Forbidden";
    // case 0x01: return "Systems ISO/IEC 14496-1";
    // case 0x02: return "Systems ISO/IEC 14496-1";
    // case 0x20: return "Visual ISO/IEC 14496-2";
    // case 0x40: return "Audio ISO/IEC 14496-3";
    // case 0x60: return "Visual ISO/IEC 13818-2 Simple Profile";
    // case 0x61: return "Visual ISO/IEC 13818-2 Main Profile";
    // case 0x62: return "Visual ISO/IEC 13818-2 SNR Profile";
    // case 0x63: return "Visual ISO/IEC 13818-2 Spatial Profile";
    // case 0x64: return "Visual ISO/IEC 13818-2 High Profile";
    // case 0x65: return "Visual ISO/IEC 13818-2 422 Profile";
    // case 0x66: return "Audio ISO/IEC 13818-7 Main Profile";
    // case 0x67: return "Audio ISO/IEC 13818-7 LC Profile";
    // case 0x68: return "Audio ISO/IEC 13818-7 SSR Profile";
    // case 0x69: return "Audio ISO/IEC 13818-3";
    // case 0x6A: return "Visual ISO/IEC 11172-2";
    // case 0x6B: return "Audio ISO/IEC 11172-3";
    // case 0x6C: return "Visual ISO/IEC 10918-1";
    unsigned object_type_indication;

#if MP4D_INFO_SUPPORTED
    /************************************************************************/
    /*                 informational public data                            */
    /************************************************************************/
    // handler_type when present in a media box, is an integer containing one of
    // the following values, or a value from a derived specification:
    // 'vide' Video track
    // 'soun' Audio track
    // 'hint' Hint track
    unsigned handler_type;

    // Track duration: 64-bit value split into 2 variables
    unsigned duration_hi;
    unsigned duration_lo;

    // duration scale: duration = timescale*seconds
    unsigned timescale;

    // Average bitrate, bits per second
    unsigned avg_bitrate_bps;

    // Track language: 3-char ISO 639-2T code: "und", "eng", "rus", "jpn" etc...
    unsigned char language[4];

    // MP4 stream type
    // case 0x00: return "Forbidden";
    // case 0x01: return "ObjectDescriptorStream";
    // case 0x02: return "ClockReferenceStream";
    // case 0x03: return "SceneDescriptionStream";
    // case 0x04: return "VisualStream";
    // case 0x05: return "AudioStream";
    // case 0x06: return "MPEG7Stream";
    // case 0x07: return "IPMPStream";
    // case 0x08: return "ObjectContentInfoStream";
    // case 0x09: return "MPEGJStream";
    unsigned stream_type;

    union
    {
        // for handler_type == 'soun' tracks
        struct
        {
            unsigned channelcount;
            unsigned samplerate_hz;
        } audio;

        // for handler_type == 'vide' tracks
        struct
        {
            unsigned width;
            unsigned height;
        } video;
    } SampleDescription;
#endif

    /************************************************************************/
    /*                 private data: MP4 indexes                            */
    /************************************************************************/
    unsigned *entry_size;

    unsigned sample_to_chunk_count;
    struct MP4D_sample_to_chunk_t_tag *sample_to_chunk;

    unsigned chunk_count;
    MP4D_file_offset_t *chunk_offset;

#if MP4D_TIMESTAMPS_SUPPORTED
    unsigned *timestamp;
    unsigned *duration;
#endif

} MP4D_track_t;

typedef struct MP4D_demux_tag
{
    /************************************************************************/
    /*                 mandatory public data                                */
    /************************************************************************/
    int64_t read_pos;
    int64_t read_size;
    MP4D_track_t *track;
    int (*read_callback)(int64_t offset, void *buffer, size_t size, void *token);
    void *token;

    unsigned track_count; // number of tracks in the movie

#if MP4D_INFO_SUPPORTED
    /************************************************************************/
    /*                 informational public data                            */
    /************************************************************************/
    // Movie duration: 64-bit value split into 2 variables
    unsigned duration_hi;
    unsigned duration_lo;

    // duration scale: duration = timescale*seconds
    unsigned timescale;

    // Metadata tag (optional)
    // Tags provided 'as-is', without any re-encoding
    struct
    {
        unsigned char *title;
        unsigned char *artist;
        unsigned char *album;
        unsigned char *year;
        unsigned char *comment;
        unsigned char *genre;
    } tag;
#endif

} MP4D_demux_t;

struct MP4D_sample_to_chunk_t_tag
{
    unsigned first_chunk;
    unsigned samples_per_chunk;
};

typedef struct
{
    void *sps_cache[MINIMP4_MAX_SPS];
    void *pps_cache[MINIMP4_MAX_PPS];
    int sps_bytes[MINIMP4_MAX_SPS];
    int pps_bytes[MINIMP4_MAX_PPS];

    int map_sps[MINIMP4_MAX_SPS];
    int map_pps[MINIMP4_MAX_PPS];

} h264_sps_id_patcher_t;

typedef struct mp4_h26x_writer_tag
{
#if MINIMP4_TRANSCODE_SPS_ID
    h264_sps_id_patcher_t sps_patcher;
#endif
    MP4E_mux_t *mux;
    int mux_track_id, is_hevc, need_vps, need_sps, need_pps, need_idr;
} mp4_h26x_writer_t;

int mp4_h26x_write_init(mp4_h26x_writer_t *h, MP4E_mux_t *mux, int width, int height, int is_hevc);
void mp4_h26x_write_close(mp4_h26x_writer_t *h);
int mp4_h26x_write_nal(mp4_h26x_writer_t *h, const unsigned char *nal, int length, unsigned timeStamp90kHz_next);

/************************************************************************/
/*          API                                                         */
/************************************************************************/

/**
*   Parse given input stream as MP4 file. Allocate and store data indexes.
*   return 1 on success, 0 on failure
*   The MP4 indexes may be stored at the end of stream, so this
*   function may parse all stream.
*   It is guaranteed that function will read/seek sequentially,
*   and will never jump back.
*/
int MP4D_open(MP4D_demux_t *mp4, int (*read_callback)(int64_t offset, void *buffer, size_t size, void *token), void *token, int64_t file_size);

/**
*   Return position and size for given sample from given track. The 'sample' is a
*   MP4 term for 'frame'
*
*   frame_bytes [OUT]   - return coded frame size in bytes
*   timestamp [OUT]     - return frame timestamp (in mp4->timescale units)
*   duration [OUT]      - return frame duration (in mp4->timescale units)
*
*   function return offset for the frame
*/
MP4D_file_offset_t MP4D_frame_offset(const MP4D_demux_t *mp4, unsigned int ntrack, unsigned int nsample, unsigned int *frame_bytes, unsigned *timestamp, unsigned *duration);

/**
*   De-allocated memory
*/
void MP4D_close(MP4D_demux_t *mp4);

/**
*   Helper functions to parse mp4.track[ntrack].dsi for H.264 SPS/PPS
*   Return pointer to internal mp4 memory, it must not be free()-ed
*
*   Example: process all SPS in MP4 file:
*       while (sps = MP4D_read_sps(mp4, num_of_avc_track, sps_count, &sps_bytes))
*       {
*           process(sps, sps_bytes);
*           sps_count++;
*       }
*/
const void *MP4D_read_sps(const MP4D_demux_t *mp4, unsigned int ntrack, int nsps, int *sps_bytes);
const void *MP4D_read_pps(const MP4D_demux_t *mp4, unsigned int ntrack, int npps, int *pps_bytes);

#if MP4D_PRINT_INFO_SUPPORTED
/**
*   Print MP4 information to stdout.
*   Uses printf() as well as floating-point functions
*   Given as implementation example and for test purposes
*/
void MP4D_printf_info(const MP4D_demux_t *mp4);
#endif

/**
*   Allocates and initialize mp4 multiplexor
*   Given file handler is transparent to the MP4 library, and used only as
*   argument for given fwrite_callback() function.  By appropriate definition
*   of callback function application may use any other file output API (for
*   example C++ streams, or Win32 file functions)
*
*   return multiplexor handle on success; NULL on failure
*/
MP4E_mux_t *MP4E_open(int sequential_mode_flag, int enable_fragmentation, void *token,
    int (*write_callback)(int64_t offset, const void *buffer, size_t size, void *token));

/**
*   Add new track
*   The track_data parameter does not referred by the multiplexer after function
*   return, and may be allocated in short-time memory. The dsi member of
*   track_data parameter is mandatory.
*
*   return ID of added track, or error code MP4E_STATUS_*
*/
int MP4E_add_track(MP4E_mux_t *mux, const MP4E_track_t *track_data);

/**
*   Add new sample to specified track
*   The tracks numbered starting with 0, according to order of MP4E_add_track() calls
*   'kind' is one of MP4E_SAMPLE_... defines
*
*   return error code MP4E_STATUS_*
*
*   Example:
*       MP4E_put_sample(mux, 0, data, data_bytes, duration, MP4E_SAMPLE_DEFAULT);
*/
int MP4E_put_sample(MP4E_mux_t *mux, int track_num, const void *data, int data_bytes, int duration, int kind);

/**
*   Finalize MP4 file, de-allocated memory, and closes MP4 multiplexer.
*   The close operation takes a time and disk space, since it writes MP4 file
*   indexes.  Please note that this function does not closes file handle,
*   which was passed to open function.
*
*   return error code MP4E_STATUS_*
*/
int MP4E_close(MP4E_mux_t *mux);

/**
*   Set Decoder Specific Info (DSI)
*   Can be used for audio and private tracks.
*   MUST be used for AAC track.
*   Only one DSI can be set. It is an error to set DSI again
*
*   return error code MP4E_STATUS_*
*/
int MP4E_set_dsi(MP4E_mux_t *mux, int track_id, const void *dsi, int bytes);

/**
*   Set VPS data. MUST be used for HEVC (H.265) track.
*
*   return error code MP4E_STATUS_*
*/
int MP4E_set_vps(MP4E_mux_t *mux, int track_id, const void *vps, int bytes);

/**
*   Set SPS data. MUST be used for AVC (H.264) track. Up to 32 different SPS can be used in one track.
*
*   return error code MP4E_STATUS_*
*/
int MP4E_set_sps(MP4E_mux_t *mux, int track_id, const void *sps, int bytes);

/**
*   Set PPS data. MUST be used for AVC (H.264) track. Up to 256 different PPS can be used in one track.
*
*   return error code MP4E_STATUS_*
*/
int MP4E_set_pps(MP4E_mux_t *mux, int track_id, const void *pps, int bytes);

/**
*   Set or replace ASCII test comment for the file. Set comment to NULL to remove comment.
*
*   return error code MP4E_STATUS_*
*/
int MP4E_set_text_comment(MP4E_mux_t *mux, const char *comment);

#ifdef __cplusplus
}
#endif
#endif //MINIMP4_H
