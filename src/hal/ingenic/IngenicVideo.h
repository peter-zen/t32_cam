#pragma once
#include "IVideo.h"
#include <imp/imp_common.h>
#include <imp/imp_framesource.h>
#include <imp/imp_isp.h>
#include <imp/imp_encoder.h>
#include <vector>
#include <memory>
#include <mutex>
#include "IspOsdManager.h"
namespace hal {

#define FS_CHN_NUM 12
#define CHN_ENABLE 1
#define CHN_DISABLE 0

typedef struct chn_conf{
    unsigned int index;
    unsigned int enable;
    IMPPayloadType  payloadType;
    IMPFSChnAttr fs_chn_attr;
    IMPCell framesource_chn;
    IMPCell imp_encoder;
} chn_conf;

struct SensorFps { int num; int den; };
struct I2CConfig { char type[32]; unsigned char addr; int adapter_id; };
struct GPIOPins { int rst; int pwdn; int power; int sw; int sw_state; };
struct SensorConfig {
    char name[32];
    int cbus_type;
    I2CConfig i2c;
    GPIOPins gpio;
    int sensor_id;
    int video_interface;
    int mclk;
    int default_boot;
    SensorFps fps;
    int width;
    int height;
};
struct ChannelConfig {
    unsigned int index;
    unsigned int enable;
    IMPFSChnAttr fs_attr;
    int sensor_index;
    int output_index;
};

class SensorController {
public:
    int openISP();
    int closeISP();
    int setCameraInputMode(const std::vector<SensorConfig>& sensors);
    int addAll(const std::vector<SensorConfig>& sensors);
    int enableAll(const std::vector<SensorConfig>& sensors);
    int setAllFps(const std::vector<SensorConfig>& sensors);
    int disableAll(const std::vector<SensorConfig>& sensors);
    int delAll(const std::vector<SensorConfig>& sensors);
private:
    IMPSensorInfo sensor_info[4];
    IMPISPCameraInputMode mode;
};
class FrameChannelController {
public:
    void init(const std::vector<ChannelConfig>& cfgs);
    int create(int index = -1);
    int setAttr(int index = -1);
    int enable(int index = -1);
    int disable(int index = -1);
    int destroy(int index = -1);
    const std::vector<ChannelConfig>& getChannels() const { return channels_; }
private:
    std::vector<ChannelConfig> channels_;
};
class OSDController {
public:
    int setPoolSize(int mode);
};

class IngenicVideoStream : public IVideoStream {
public:
    IngenicVideoStream();
    ~IngenicVideoStream() override;
    bool configure(const VideoStreamConfig& cfg) override;
    bool start() override;
    bool stop() override;
    bool polling(int timeout_ms) override;
    bool getFrame(VideoEncodedFrame& out) override;
    void releaseFrame(VideoEncodedFrame& out) override;
    bool getInfo(VideoStreamInfo& info) override;
    bool requestIDR() override;
private:
    VideoStreamConfig cfg_;
    bool configured_;
    bool started_;
    int ref_count_;
    std::mutex mtx_;
    int group_id_;
    int channel_id_;
    IMPCell fs_cell_;
    IMPCell enc_cell_;
    IMPEncoderStream last_stream_;
    bool last_stream_valid_ = false;
    std::vector<VideoEncodedPiece> last_pieces_;
};
class IngenicVideo : public IVideo {
public:
    IngenicVideo();
    ~IngenicVideo() override;
    bool init() override;
    bool exit() override;
    std::shared_ptr<IVideoStream> createVideoStream() override;
private:
    SensorController sensorMgr;
    FrameChannelController fsMgr;
    chn_conf chn_[FS_CHN_NUM];
    int direct_switch_;
    int gosd_enable_;
    bool exitCalled_;
    std::unique_ptr<IspOsdManager> ispOsdMgr_;
};

class IngenicVideoControl : public IVideoControl {
public:
    IngenicVideoControl();
    ~IngenicVideoControl() override;
    bool getISPMode(ISPDaynightMode& state) override;
    bool setISPMode(ISPDaynightMode state) override;
private:
};
} 
