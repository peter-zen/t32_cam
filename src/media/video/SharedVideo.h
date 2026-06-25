#pragma once
// 进程级 IngenicVideo 单例。VideoRecorder(record) + ImageSnap(photo) 共用同一实例，
// 进程内永不 IMP_System_Exit（避免 cm==1 photo→record 的 exit→re-Init kernel wedge）。
// channel 级释放（CreateChn/DestroyChn）由各 IVideoStream 析构完成；本单例 static 持有
// 一份 ref → 进程结束前不析构、不 exit。
//
// 必须是单 TU（SharedVideo.cpp）定义、跨 .so 共享——不能用 inline header（每个 .so 会
// 各得一份 static local → 两实例 → 双 IMP_System_Init → wedge）。
#include <memory>
#include "IVideo.h"       // hal::IVideo

namespace media {

// 返回进程级唯一 IngenicVideo（首次调用 createVideo+init；后续直接返回）。永不为 exit。
std::shared_ptr<hal::IVideo> sharedVideo();

// 清掉单例引用（无人持有时 ~IngenicVideo → exit → IMP_System_Exit）；下次 sharedVideo()
// 重新 init（fresh IMP session）。cm==1 用来在 photo/record 间做完整 IMP reset
// （逼近「分开 app」的 fresh-session-per-capture）。见 capture_lane + HTC_CM1_RESET。
void resetSharedVideo();

}  // namespace media
