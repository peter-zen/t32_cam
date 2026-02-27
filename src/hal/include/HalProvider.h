#pragma once
#include <memory>
#include "IVideo.h"
#include "IAudio.h"
#include "IGpio.h"
namespace hal {
class HalProvider {
public:
    static std::shared_ptr<IVideo> createVideo();
    static std::shared_ptr<IAudio> createAudio();
    static std::shared_ptr<IVideoControl> createVideoControl();
    static std::shared_ptr<IGpio> createGpio();
};
}
