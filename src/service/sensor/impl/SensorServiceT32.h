#ifndef SENSOR_SERVICE_T32_H
#define SENSOR_SERVICE_T32_H

#include "../ISensorService.h"

namespace service {

class SensorServiceT32 : public ISensorService {
public:
    SensorData getSensorData() override;
};

} // namespace service

#endif // SENSOR_SERVICE_T32_H
