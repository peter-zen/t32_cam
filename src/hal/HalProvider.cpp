#include "HalProvider.h"
#include "ingenic/IngenicVideo.h"
#include "ingenic/IngenicAudio.h"
#include "ingenic/IngenicGpio.h"
#include "simu/SimVideo.h"
#include "simu/SimAudio.h"
#include "simu/SimGpio.h"
namespace hal {
std::shared_ptr<IVideo> HalProvider::createVideo() {
    #ifdef BUILD_FOR_SIMULATION
        return std::make_shared<SimVideo>();
    #else
        return std::make_shared<IngenicVideo>();
    #endif
}
std::shared_ptr<IAudio> HalProvider::createAudio() {
    #ifdef BUILD_FOR_SIMULATION
        return std::make_shared<SimAudio>();
    #else
        return std::make_shared<IngenicAudio>();
    #endif
}
std::shared_ptr<IVideoControl> HalProvider::createVideoControl() {
    #ifdef BUILD_FOR_SIMULATION
        return std::make_shared<SimVideoControl>();
    #else
        return std::make_shared<IngenicVideoControl>();
    #endif
}
std::shared_ptr<IGpio> HalProvider::createGpio() {
    #ifdef BUILD_FOR_SIMULATION
        return std::make_shared<SimGpio>();
    #else
        return std::make_shared<IngenicGpio>();
    #endif
}
}
