#include "DmicIn.h"
#include <imp/imp_dmic.h>
#include <imp/imp_audio.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <chrono>
#include "Logger.h"
#include "Misc.h"

using namespace media;

void DmicIn::recordThreadFunc() {
    int ret = 0;
    int recordNum = 0;
    IMPDmicChnFrame chnFrm;
    
    while (this->threadRunning) {
        // 轮询DMIC音频帧
        ret = IMP_DMIC_PollingFrame(audioParams.getDeviceId(), audioParams.getChannelId(), 1000);
        if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[DmicIn] Polling frame failed: %d\n", ret);
            continue;
        }
        
        // 获取DMIC音频帧
        ret = IMP_DMIC_GetFrame(audioParams.getDeviceId(), audioParams.getChannelId(), &chnFrm, BLOCK);
        if (ret != 0) {
            Logger::log(LogLevel::ERROR, "[DmicIn] Get frame failed: %d\n", ret);
            continue;
        }
        
        // 更新当前时间戳（基于采样率和帧数计算）
        int sampleRate = audioParams.getSampleRateValue();
        int numPerFrame = audioParams.getNumPerFrame();
        if (sampleRate > 0 && numPerFrame > 0) {
            currentTimestamp += (uint64_t)(numPerFrame * 1000) / sampleRate;
        }
        
        if (aencEnabled) {
            IMPAudioFrame frm;
            memset(&frm, 0, sizeof(frm));
            frm.bitwidth = AUDIO_BIT_WIDTH_16;
            frm.soundmode = static_cast<IMPAudioSoundMode>(audioParams.getSoundModeValue());
            frm.virAddr = reinterpret_cast<uint32_t*>(chnFrm.rawFrame.virAddr);
            frm.len = chnFrm.rawFrame.len;
            ret = IMP_AENC_SendFrame(aencChn, &frm);
            if (ret != 0) {
                Logger::log(LogLevel::ERROR, "[DmicIn] Send frame to AENC failed: %d\n", ret);
            } else {
                for (int i = 0; i < 8; ++i) {
                    ret = IMP_AENC_PollingStream(aencChn, 0);
                    if (ret != 0) {
                        break;
                    }

                    IMPAudioStream stream;
                    memset(&stream, 0, sizeof(stream));
                    ret = IMP_AENC_GetStream(aencChn, &stream, NOBLOCK);
                    if (ret != 0) {
                        break;
                    }

                    if (this->recordFile && stream.len > 0) {
                        fwrite(stream.stream, 1, stream.len, this->recordFile);
                    }
                    if (audioDataCallback && stream.len > 0) {
                        audioDataCallback(
                            reinterpret_cast<const uint8_t*>(stream.stream),
                            stream.len,
                            currentTimestamp,
                            false,
                            audioDataCallbackUserData
                        );
                    }

                    ret = IMP_AENC_ReleaseStream(aencChn, &stream);
                    if (ret != 0) {
                        Logger::log(LogLevel::ERROR, "[DmicIn] Release AENC stream failed: %d\n", ret);
                        break;
                    }
                }
            }
        } else {
            AudioCodecFormat codec = audioParams.getCodecFormat();
            if (codec == AudioCodecFormat::AAC && aacEncoder.context() != nullptr) {
                int bytesPerSample = 2;
                int numSamples = chnFrm.rawFrame.len / bytesPerSample;
                if (numSamples > 0) {
                    unsigned char outbuf[8192];
                    int outLen = 0;
                    int encRet = 0;
                        const int16_t* pcmData = reinterpret_cast<const int16_t*>(chnFrm.rawFrame.virAddr);
                        encRet = aacEncoder.encode(pcmData, numSamples, outbuf, static_cast<int>(sizeof(outbuf)), &outLen);
                    if (encRet != 0) {
                        Logger::log(LogLevel::ERROR, "[DmicIn] AAC encode failed: %d\n", encRet);
                    } else if (outLen > 0) {
                        if (this->recordFile) {
                            fwrite(outbuf, 1, outLen, this->recordFile);
                        }
                        if (audioDataCallback) {
                            audioDataCallback(
                                reinterpret_cast<const uint8_t*>(outbuf),
                                static_cast<size_t>(outLen),
                                currentTimestamp,
                                false,
                                audioDataCallbackUserData
                            );
                        }
                    }
                }
            } else {
                if (this->recordFile) {
                    short* pdata = reinterpret_cast<short*>(chnFrm.rawFrame.virAddr);
                    int channelCount = audioParams.getChannelCount();
                    for (int k = 0; k < audioParams.getNumPerFrame(); k++) {
                        for (int ch = 0; ch < channelCount; ch++) {
                            fwrite(&pdata[k * channelCount + ch], 2, 1, this->recordFile);
                        }
                    }
                }
                if (audioDataCallback) {
                    audioDataCallback(
                        reinterpret_cast<const uint8_t*>(chnFrm.rawFrame.virAddr),
                        chnFrm.rawFrame.len,
                        currentTimestamp,
                        false,
                        audioDataCallbackUserData
                    );
                }
            }
        }
        
        // 释放DMIC音频帧
        ret = IMP_DMIC_ReleaseFrame(audioParams.getDeviceId(), audioParams.getChannelId(), &chnFrm);
        if (ret != 0) {
            Logger::log(LogLevel::ERROR, "[DmicIn] Release frame failed: %d\n", ret);
        }
        
        recordNum++;
    }
}

DmicIn::DmicIn() : 
    isRecording_(false),
    recordFilePath(""),
    recordFile(nullptr),
    recordThread(nullptr),
    threadRunning(false),
    audioDataCallback(nullptr),
    audioDataCallbackUserData(nullptr),
    currentTimestamp(0),
    aencEnabled(false),
    aencChn(0) {
    audioParams.setDeviceId(0);
    audioParams.setChannelId(0);
}

DmicIn::~DmicIn() {
    stop();
}

bool DmicIn::start() {
    if (isRecording_) {
        return false;
    }
    
    // 打开录音文件（可选操作，如果用户不需要保存文件，这里会失败但不影响录音）
    if (!recordFilePath.empty()) {
        recordFile = fopen(recordFilePath.c_str(), "wb");
        if (!recordFile) {
            Logger::log(LogLevel::ERROR, "[DmicIn] Open record file failed: %s\n", recordFilePath.c_str());
        }
    } else {
        recordFile = nullptr;
    }
    
    // 设置麦克阵列参数
    int ret = IMP_DMIC_SetUserInfo(audioParams.getDeviceId(), audioParams.getAecDmicId(), audioParams.isNeedAec() ? 1 : 0);
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[DmicIn] Set user info failed: %d\n", ret);
        Logger::log(LogLevel::ERROR, "[DmicIn] DeviceId: %d, AecDmicId: %d, NeedAec: %d\n", 
                    audioParams.getDeviceId(), audioParams.getAecDmicId(), audioParams.isNeedAec() ? 1 : 0);
        // 继续执行，即使设置用户信息失败也尝试启动录音
        // fclose(recordFile);
        // recordFile = nullptr;
        // return false;
    }
    
    // 设置DMIC设备属性
    IMPDmicAttr attr;
    attr.samplerate = static_cast<IMPDmicSampleRate>(audioParams.getSampleRateValue());
    attr.bitwidth = static_cast<IMPDmicBitWidth>(audioParams.getBitWidthValue());
    attr.soundmode = static_cast<IMPDmicSoundMode>(audioParams.getSoundModeValue());
    attr.frmNum = audioParams.getFrameNum();
    attr.numPerFrm = audioParams.getNumPerFrame();
    attr.chnCnt = audioParams.getChannelCount();

    ret = IMP_DMIC_SetPubAttr(audioParams.getDeviceId(), &attr);
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[DmicIn] Set pub attr failed: %d\n", ret);
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }
    
    // 启用DMIC设备
    ret = IMP_DMIC_Enable(audioParams.getDeviceId());
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[DmicIn] Enable device failed: %d\n", ret);
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }
    
    // 设置DMIC通道参数
    IMPDmicChnParam chnParam;
    chnParam.usrFrmDepth = audioParams.getUsrFrmDepth();
    chnParam.Rev = 0;
    
    ret = IMP_DMIC_SetChnParam(audioParams.getDeviceId(), audioParams.getChannelId(), &chnParam);
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[DmicIn] Set channel param failed: %d\n", ret);
        IMP_DMIC_Disable(audioParams.getDeviceId());
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }
    
    // 启用DMIC通道
    ret = IMP_DMIC_EnableChn(audioParams.getDeviceId(), audioParams.getChannelId());
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[DmicIn] Enable channel failed: %d\n", ret);
        IMP_DMIC_Disable(audioParams.getDeviceId());
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }
    
    // 设置DMIC音量
    ret = IMP_DMIC_SetVol(audioParams.getDeviceId(), audioParams.getChannelId(), audioParams.getVolume());
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[DmicIn] Set volume failed: %d\n", ret);
        IMP_DMIC_DisableChn(audioParams.getDeviceId(), audioParams.getChannelId());
        IMP_DMIC_Disable(audioParams.getDeviceId());
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }
    
    // 设置DMIC增益
    ret = IMP_DMIC_SetGain(audioParams.getDeviceId(), audioParams.getChannelId(), audioParams.getGain());
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[DmicIn] Set gain failed: %d\n", ret);
        IMP_DMIC_DisableChn(audioParams.getDeviceId(), audioParams.getChannelId());
        IMP_DMIC_Disable(audioParams.getDeviceId());
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }

    aencEnabled = false;
    aencChn = audioParams.getChannelId();

    AudioCodecFormat codec = audioParams.getCodecFormat();
    Logger::log(
        LogLevel::INFO,
        "[DmicIn] Start with codec=%d sampleRate=%d numPerFrame=%d frameNum=%d soundMode=%d\n",
        static_cast<int>(codec),
        audioParams.getSampleRateValue(),
        audioParams.getNumPerFrame(),
        audioParams.getFrameNum(),
        audioParams.getSoundModeValue()
    );
    if (codec == AudioCodecFormat::G711A || codec == AudioCodecFormat::G711U) {
        IMPAudioEncChnAttr aencAttr;
        memset(&aencAttr, 0, sizeof(aencAttr));

        if (codec == AudioCodecFormat::G711A) {
            aencAttr.type = PT_G711A;
        } else {
            aencAttr.type = PT_G711U;
        }

        if (aencAttr.type != 0) {
            aencAttr.bufSize = 20;
            ret = IMP_AENC_CreateChn(aencChn, &aencAttr);
            if (ret != 0) {
                Logger::log(LogLevel::ERROR, "[DmicIn] Create AENC channel failed: %d\n", ret);
            } else {
                aencEnabled = true;
            }
        }
    } else if (codec == AudioCodecFormat::AAC) {
        int channels = audioParams.getChannelCount();
        AacEncoder::QualityProfile profile = AacEncoder::QualityProfile::VOICE;
        AacQualityProfile ap = audioParams.getAacQualityProfile();
        if (ap == AacQualityProfile::ENVIRONMENT) {
            profile = AacEncoder::QualityProfile::ENVIRONMENT;
        } else if (ap == AacQualityProfile::MUSIC_HIGH) {
            profile = AacEncoder::QualityProfile::MUSIC_HIGH;
        }
        ret = aacEncoder.openWithProfileBitWidth(
            audioParams.getSampleRateValue(),
            channels,
            audioParams.getAacBitRatePerChannel(),
            profile,
            audioParams.getBitWidthValue()
        );
        if (ret != 0 || aacEncoder.context() == nullptr) {
            Logger::log(LogLevel::ERROR, "[DmicIn] Open AAC encoder failed: %d\n", ret);
        }
    }

    if (codec == AudioCodecFormat::AAC && aacEncoder.context() == nullptr) {
        Logger::log(LogLevel::ERROR, "[DmicIn] AAC requested but software encoder is not enabled\n");
        IMP_DMIC_DisableChn(audioParams.getDeviceId(), audioParams.getChannelId());
        IMP_DMIC_Disable(audioParams.getDeviceId());
        aacEncoder.close();
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
            if (!recordFilePath.empty()) {
                remove(recordFilePath.c_str());
            }
        }
        return false;
    }
    
    // 启动DMIC录音线程
    this->threadRunning = true;
    
    try {
        this->recordThread = std::make_shared<std::thread>(&DmicIn::recordThreadFunc, this);
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR, "[DmicIn] Create record thread failed: %s\n", e.what());
        IMP_DMIC_DisableChn(audioParams.getDeviceId(), audioParams.getChannelId());
        IMP_DMIC_Disable(audioParams.getDeviceId());
        if (aencEnabled) {
            IMP_AENC_DestroyChn(aencChn);
            aencEnabled = false;
        }
        aacEncoder.close();
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        this->threadRunning = false;
        return false;
    }
    
    isRecording_ = true;
    return true;
}

bool DmicIn::stop() {
    if (!isRecording_) {
        return false;
    }
    isRecording_ = false;
    
    // 停止定时器线程
    if (timerThread) {
        if (timerThread->get_id() == std::this_thread::get_id()) {
            timerThread->detach();
        } else {
            timerThread->join();
        }
        timerThread.reset();
    }
    
    // 停止录音线程
    if (this->recordThread) {
        this->threadRunning = false;
        this->recordThread->join();
        this->recordThread.reset();
    }
    
    // 禁用DMIC通道
    int ret = IMP_DMIC_DisableChn(audioParams.getDeviceId(), audioParams.getChannelId());
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[DmicIn] Disable channel failed: %d\n", ret);
    }
    
    // 禁用DMIC设备
    ret = IMP_DMIC_Disable(audioParams.getDeviceId());
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[DmicIn] Disable device failed: %d\n", ret);
    }

    if (aencEnabled) {
        for (int i = 0; i < 64; ++i) {
            int pret = IMP_AENC_PollingStream(aencChn, 0);
            if (pret != 0) {
                break;
            }
            IMPAudioStream stream;
            memset(&stream, 0, sizeof(stream));
            int gret = IMP_AENC_GetStream(aencChn, &stream, NOBLOCK);
            if (gret != 0) {
                break;
            }
            IMP_AENC_ReleaseStream(aencChn, &stream);
        }
        int dret = IMP_AENC_DestroyChn(aencChn);
        if (dret != 0) {
            Logger::log(LogLevel::ERROR, "[DmicIn] Destroy AENC channel failed: %d\n", dret);
        }
        aencEnabled = false;
    }
    if (aacEncoder.context() != nullptr) {
        aacEncoder.close();
    }
    
    // 关闭录音文件
    if (recordFile) {
        fclose(recordFile);
        recordFile = nullptr;
    }
    
    return true;
}

bool DmicIn::isRecording() const {
    return isRecording_;
}

bool DmicIn::recordFor(int durationMs) {
    // 如果已经在录音，返回false
    if (isRecording_) {
        Logger::log(LogLevel::WARNING, "[DmicIn] Already recording\n");
        return false;
    }
    
    // 开始录音
    if (!start()) {
        Logger::log(LogLevel::ERROR, "[DmicIn] Failed to start recording\n");
        return false;
    }
    
    // 创建定时器线程，非阻塞等待指定时长后停止录音
    try {
        timerThread = std::make_shared<std::thread>([this, durationMs]() {
            // 等待指定时长
            std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
            
            // 停止录音
            stop();
            (void)durationMs;
        });
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR, "[DmicIn] Create timer thread failed: %s\n", e.what());
        stop();
        return false;
    }
    
    // 非阻塞，立即返回
    return true;
}

void DmicIn::setAudioParams(const AudioParams& params) {
    audioParams = params;
}

void DmicIn::setRecordFilePath(const std::string& filePath) {
    recordFilePath = filePath;
}

void DmicIn::setAudioDataCallback(AudioDataCallback callback, void* userData) {
    audioDataCallback = callback;
    audioDataCallbackUserData = userData;
}

uint64_t DmicIn::getCurrentTimestamp() const {
    return currentTimestamp;
}

bool DmicIn::loadDriver() {
    std::string checkCommand = "lsmod | grep audio";
    int ret = Misc::syscall(checkCommand.c_str());
    if (ret == 0) {
        return true;
    }
    // 驱动未加载，尝试加载DMIC驱动模块
    std::string command = "insmod /system/modules/audio/audio.ko";
    command += " dmic_enable=1 dmic_gpio=1";//enable dmic with PB group(PB28/29/30) gpio
    command += " spk_gpio=-1 spk_level=-1";
    
    ret = Misc::syscall(command.c_str());
    if (ret != 0) {
        (void)ret;
        // 驱动加载失败不影响后续操作，可能驱动已经由其他方式加载
        return true;
    }

    return true;
}
