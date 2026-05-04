#ifndef SENSOR_SERVICE_SIM_H
#define SENSOR_SERVICE_SIM_H

#include "../ISensorService.h"

namespace service {

class SensorServiceSim : public ISensorService {
public:
    SensorData getSensorData() override;
};

} // namespace service

#endif // SENSOR_SERVICE_SIM_H
