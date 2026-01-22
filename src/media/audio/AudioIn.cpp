#include "AudioIn.h"
#include <imp/imp_audio.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <chrono>
#include <vector>
#include "Logger.h"
#include "Misc.h"

using namespace media;

AudioIn::AudioIn() : 
    isRecording_(false),
    recordFilePath(""),
    recordFile(nullptr),
    recordThread(nullptr),
    threadRunning(false),
    audioDataCallback(nullptr),
    audioDataCallbackUserData(nullptr),
    currentTimestamp(0),
    aencEnabled(false),
    aencChn(0),
    actualChnCnt(0),
    actualSampleRate(0) {
    audioParams.setDeviceId(0);
    audioParams.setChannelId(0);
}

void AudioIn::recordThreadFunc() {
    // 实现录音逻辑
    int ret = 0;
    int recordNum = 0;
    IMPAudioFrame frm;
    
    auto lastCheckTime = std::chrono::steady_clock::now();
    int framesInLastSecond = 0;
    int samplesInLastSecond = 0;
    
    while (threadRunning) {
        // 轮询音频帧
        ret = IMP_AI_PollingFrame(audioParams.getDeviceId(), audioParams.getChannelId(), 1000);
        if (ret != 0) {
            Logger::log(LogLevel::ERROR, "[AudioIn] Polling frame failed: %d\n", ret);
            continue;
        }
        
        auto now = std::chrono::steady_clock::now();

        // 获取音频帧
        ret = IMP_AI_GetFrame(audioParams.getDeviceId(), audioParams.getChannelId(), &frm, BLOCK);
        if (ret != 0) {
            Logger::log(LogLevel::ERROR, "[AudioIn] Get frame failed: %d\n", ret);
            continue;
        }
        
        // 更新当前时间戳（基于采样率和帧数计算）
        int sampleRate = (actualSampleRate > 0) ? actualSampleRate : audioParams.getSampleRateValue();
        int numPerFrame = audioParams.getNumPerFrame();
        if (sampleRate > 0 && numPerFrame > 0) {
            currentTimestamp += (uint64_t)(numPerFrame * 1000) / sampleRate;
        }
        
        if (aencEnabled) {
            ret = IMP_AENC_SendFrame(aencChn, &frm);
            if (ret != 0) {
                Logger::log(LogLevel::ERROR, "[AudioIn] Send frame to AENC failed: %d\n", ret);
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

                    if (stream.len > 0) {
                        if (recordFile) {
                            fwrite(stream.stream, 1, stream.len, recordFile);
                        }
                        if (audioDataCallback) {
                            audioDataCallback(
                                reinterpret_cast<const uint8_t*>(stream.stream),
                                stream.len,
                                currentTimestamp,
                                false,
                                audioDataCallbackUserData
                            );
                        }
                    }

                    ret = IMP_AENC_ReleaseStream(aencChn, &stream);
                    if (ret != 0) {
                        Logger::log(LogLevel::ERROR, "[AudioIn] Release AENC stream failed: %d\n", ret);
                        break;
                    }
                }
            }
        } else {
            AudioCodecFormat codec = audioParams.getCodecFormat();
            if (codec == AudioCodecFormat::AAC && aacEncoder.context() != nullptr) {
                int bytesPerSample = 2;
                int numSamples = frm.len / bytesPerSample;

                std::vector<int16_t> monoBuffer;
                if (audioParams.getSoundMode() == AudioSoundMode::MONO) {
                    int expectedSamples = audioParams.getNumPerFrame();
                    bool mismatchDetected = false;
                    
                    if (actualChnCnt == 2 || frm.soundmode == AUDIO_SOUND_MODE_STEREO) {
                        mismatchDetected = true;
                    }
                    else if (numSamples >= expectedSamples * 2 - 10 && numSamples <= expectedSamples * 2 + 10) {
                        mismatchDetected = true;
                    }

                    if (mismatchDetected) {
                            const int16_t* pcmData16 = reinterpret_cast<const int16_t*>(frm.virAddr);
                            monoBuffer.reserve(numSamples / 2);
                            for (int i = 0; i < numSamples; i += 2) {
                                monoBuffer.push_back(pcmData16[i]);
                            }
                            numSamples = static_cast<int>(monoBuffer.size());
                    }
                }

                if (numSamples > 0) {
                    unsigned char outbuf[8192];
                    int outLen = 0;
                    int encRet = 0;
                        const int16_t* src = monoBuffer.empty() ? reinterpret_cast<const int16_t*>(frm.virAddr) : monoBuffer.data();
                        encRet = aacEncoder.encode(src, numSamples, outbuf, static_cast<int>(sizeof(outbuf)), &outLen);
                    if (encRet != 0) {
                        Logger::log(LogLevel::ERROR, "[AudioIn] AAC encode failed: %d\n", encRet);
                    } else if (outLen > 0) {
                        if (recordFile) {
                            fwrite(outbuf, 1, outLen, recordFile);
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
                if (recordFile) {
                    fwrite(frm.virAddr, 1, frm.len, recordFile);
                }
                if (audioDataCallback) {
                    audioDataCallback(
                        reinterpret_cast<const uint8_t*>(frm.virAddr),
                        frm.len,
                        currentTimestamp,
                        false,
                        audioDataCallbackUserData
                    );
                }
            }
        }
        
        // 释放音频帧
        ret = IMP_AI_ReleaseFrame(audioParams.getDeviceId(), audioParams.getChannelId(), &frm);
        if (ret != 0) {
            Logger::log(LogLevel::ERROR, "[AudioIn] Release frame failed: %d\n", ret);
        }
        
        recordNum++;

        framesInLastSecond++;
        samplesInLastSecond += (frm.len / (audioParams.getBitWidthValue() / 8)) / (actualChnCnt > 0 ? actualChnCnt : 1);
        now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCheckTime).count();
        if (diff >= 1000) {
            framesInLastSecond = 0;
            samplesInLastSecond = 0;
            lastCheckTime = now;
        }
    }
}

AudioIn::~AudioIn() {
    stop();
}

bool AudioIn::start() {
    if (isRecording_) {
        Logger::log(LogLevel::WARNING, "[AudioIn] Already recording\n");
        return false;
    }
    
    if (!recordFilePath.empty()) {
        recordFile = fopen(recordFilePath.c_str(), "wb");
        if (!recordFile) {
            Logger::log(LogLevel::ERROR, "[AudioIn] Open record file failed: %s\n", recordFilePath.c_str());
        }
    } else {
        recordFile = nullptr;
    }
    
    IMPAudioIOAttr attr;
    attr.samplerate = static_cast<IMPAudioSampleRate>(audioParams.getSampleRateValue());
    attr.bitwidth = static_cast<IMPAudioBitWidth>(audioParams.getBitWidthValue());
    attr.soundmode = static_cast<IMPAudioSoundMode>(audioParams.getSoundModeValue());
    attr.frmNum = audioParams.getFrameNum();
    attr.numPerFrm = audioParams.getNumPerFrame();
    attr.chnCnt = (audioParams.getSoundMode() == AudioSoundMode::MONO) ? 1 : 2;
    
    int ret = IMP_AI_SetPubAttr(audioParams.getDeviceId(), &attr);
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[AudioIn] Set pub attr failed: %d\n", ret);
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }
    
    ret = IMP_AI_Enable(audioParams.getDeviceId());
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[AudioIn] Enable device failed: %d\n", ret);
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }

    IMPAudioIOAttr currentAttr;
    int getAttrRet = IMP_AI_GetPubAttr(audioParams.getDeviceId(), &currentAttr);
    if (getAttrRet == 0) {
        actualChnCnt = currentAttr.chnCnt;
        actualSampleRate = static_cast<int>(currentAttr.samplerate);
    } else {
        actualChnCnt = 2; // Default to Stereo if failed, to be safe
        actualSampleRate = audioParams.getSampleRateValue();
        Logger::log(LogLevel::ERROR, "[AudioIn] Failed to GetPubAttr: %d\n", getAttrRet);
    }
    
    ret = IMP_AI_SetVol(audioParams.getDeviceId(), audioParams.getChannelId(), audioParams.getVolume());
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[AudioIn] Set volume failed: %d\n", ret);
        IMP_AI_Disable(audioParams.getDeviceId());
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }
    
    ret = IMP_AI_SetGain(audioParams.getDeviceId(), audioParams.getChannelId(), audioParams.getGain());
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[AudioIn] Set gain failed: %d\n", ret);
        IMP_AI_Disable(audioParams.getDeviceId());
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }
    
    IMPAudioIChnParam chnParam;
    chnParam.usrFrmDepth = audioParams.getFrameNum();
    chnParam.aecChn = AUDIO_AEC_CHANNEL_FIRST_LEFT; // 使用默认通道，不使用AEC时也需要设置
    ret = IMP_AI_SetChnParam(audioParams.getDeviceId(), audioParams.getChannelId(), &chnParam);
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[AudioIn] Set channel param failed: %d\n", ret);
        IMP_AI_Disable(audioParams.getDeviceId());
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }
    
    ret = IMP_AI_EnableChn(audioParams.getDeviceId(), audioParams.getChannelId());
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[AudioIn] Enable channel failed: %d\n", ret);
        IMP_AI_Disable(audioParams.getDeviceId());
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }

    aencEnabled = false;
    aencChn = audioParams.getChannelId();

    AudioCodecFormat codec = audioParams.getCodecFormat();
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
                Logger::log(LogLevel::ERROR, "[AudioIn] Create AENC channel failed: %d\n", ret);
            } else {
                aencEnabled = true;
            }
        }
    } else if (codec == AudioCodecFormat::AAC) {
        int channelsWanted = (audioParams.getSoundMode() == AudioSoundMode::MONO) ? 1 : 2;
        int channels = channelsWanted;
        if (actualChnCnt > 0 && channelsWanted > actualChnCnt) {
            channels = actualChnCnt;
        }
        AacEncoder::QualityProfile profile = AacEncoder::QualityProfile::VOICE;
        AacQualityProfile ap = audioParams.getAacQualityProfile();
        if (ap == AacQualityProfile::ENVIRONMENT) {
            profile = AacEncoder::QualityProfile::ENVIRONMENT;
        } else if (ap == AacQualityProfile::MUSIC_HIGH) {
            profile = AacEncoder::QualityProfile::MUSIC_HIGH;
        }
        int inputSampleRate = (actualSampleRate > 0) ? actualSampleRate : audioParams.getSampleRateValue();
        int encoderSampleRate = inputSampleRate;
        ret = aacEncoder.openWithProfileBitWidth(
            encoderSampleRate,
            channels,
            audioParams.getAacBitRatePerChannel(),
            profile,
            audioParams.getBitWidthValue()
        );
        if (ret != 0 || aacEncoder.context() == nullptr) {
            Logger::log(LogLevel::ERROR, "[AudioIn] Open AAC encoder failed: %d\n", ret);
        }
    }

    if (codec == AudioCodecFormat::AAC && aacEncoder.context() == nullptr) {
        Logger::log(LogLevel::ERROR, "[AudioIn] AAC requested but software encoder is not enabled\n");
        IMP_AI_DisableChn(audioParams.getDeviceId(), audioParams.getChannelId());
        IMP_AI_Disable(audioParams.getDeviceId());
        if (aencEnabled) {
            int dret = IMP_AENC_DestroyChn(aencChn);
            if (dret != 0) {
                Logger::log(LogLevel::ERROR, "[AudioIn] Destroy AENC channel failed: %d\n", dret);
            }
            aencEnabled = false;
        }
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
    
    threadRunning = true;
    
    try {
        recordThread = std::make_shared<std::thread>(&AudioIn::recordThreadFunc, this);
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR, "[AudioIn] Create record thread failed: %s\n", e.what());
        IMP_AI_DisableChn(audioParams.getDeviceId(), audioParams.getChannelId());
        IMP_AI_Disable(audioParams.getDeviceId());
        if (aencEnabled) {
            IMP_AENC_DestroyChn(aencChn);
            aencEnabled = false;
        }
        aacEncoder.close();
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        threadRunning = false;
        return false;
    }
    
    isRecording_ = true;
    return true;
}

bool AudioIn::stop() {
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
    threadRunning = false;
    if (recordThread) {
        recordThread->join();
        recordThread.reset();
    }
    
    // 禁用AI通道
    int ret = IMP_AI_DisableChn(audioParams.getDeviceId(), audioParams.getChannelId());
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[AudioIn] Disable channel failed: %d\n", ret);
    }
    
    // 禁用AI设备
    ret = IMP_AI_Disable(audioParams.getDeviceId());
    if (ret != 0) {
        Logger::log(LogLevel::ERROR, "[AudioIn] Disable device failed: %d\n", ret);
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
            if (recordFile && stream.len > 0) {
                fwrite(stream.stream, 1, stream.len, recordFile);
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
            IMP_AENC_ReleaseStream(aencChn, &stream);
        }
        int dret = IMP_AENC_DestroyChn(aencChn);
        if (dret != 0) {
            Logger::log(LogLevel::ERROR, "[AudioIn] Destroy AENC channel failed: %d\n", dret);
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

bool AudioIn::isRecording() const {
    return isRecording_;
}

bool AudioIn::recordFor(int durationMs) {
    // 如果已经在录音，返回false
    if (isRecording_) {
        return false;
    }
    
    // 开始录音
    if (!start()) {
        return false;
    }
    
    // 创建定时器线程，非阻塞等待指定时长后停止录音
    try {
        timerThread = std::make_shared<std::thread>([this, durationMs]() {
            // 等待指定时长
            std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
            
            // 停止录音
            stop();
        });
    } catch (const std::exception& e) {
        stop();
        return false;
    }
    
    // 非阻塞，立即返回
    return true;
}

void AudioIn::setAudioParams(const AudioParams& params) {
    audioParams = params;
}

void AudioIn::setRecordFilePath(const std::string& filePath) {
    recordFilePath = filePath;
}

void AudioIn::setAudioDataCallback(AudioDataCallback callback, void* userData) {
    audioDataCallback = callback;
    audioDataCallbackUserData = userData;
}

uint64_t AudioIn::getCurrentTimestamp() const {
    return currentTimestamp;
}

bool AudioIn::loadDriver() {
    std::string checkCommand = "lsmod | grep audio";
    int ret = Misc::syscall(checkCommand.c_str());
    if (ret == 0) {
        return true;
    }
    // 驱动未加载，尝试加载DMIC驱动模块
    std::string command = "insmod /system/modules/audio/audio.ko";
    command += " spk_gpio=-1 spk_level=-1";
    
    ret = Misc::syscall(command.c_str());
    if (ret != 0) {
        // 驱动加载失败不影响后续操作，可能驱动已经由其他方式加载
        return true;
    }

    return true;
}
