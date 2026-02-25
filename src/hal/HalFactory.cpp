#include "HalFactory.h"
#include "video/ingenic/IngenicVideo.h"
#include "audio/ingenic/IngenicAudio.h"
#include "video/stub/SimVideo.h"
#include "audio/stub/SimAudio.h"
#include "gpio/ingenic/IngenicGpio.h"
#include "gpio/stub/SimGpio.h"


namespace hal {

std::shared_ptr<IVideo> HalFactory::createVideo() {
    #ifdef BUILD_FOR_SIMULATION
        return std::make_shared<SimVideo>();
    #else
        return std::make_shared<IngenicVideo>();
    #endif
}

std::shared_ptr<IAudio> HalFactory::createAudio() {
    #ifdef BUILD_FOR_SIMULATION
        return std::make_shared<SimAudio>();
    #else
        return std::make_shared<IngenicAudio>();
    #endif
}

std::shared_ptr<IVideoControl> HalFactory::createVideoControl() {
    #ifdef BUILD_FOR_SIMULATION
        return std::make_shared<SimVideoControl>();
    #else
        return std::make_shared<IngenicVideoControl>();
    #endif
}

std::shared_ptr<IGpio> HalFactory::createGpio() {
    #ifdef BUILD_FOR_SIMULATION
        return std::make_shared<SimGpio>();
    #else
        return std::make_shared<IngenicGpio>();
    #endif
}

}
