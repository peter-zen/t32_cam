#ifndef I_SENSOR_SERVICE_H
#define I_SENSOR_SERVICE_H

#include "../ServiceTypes.h"

namespace service {

class ISensorService {
public:
    virtual ~ISensorService() = default;
    virtual SensorData getSensorData() = 0;
};

} // namespace service

#endif // I_SENSOR_SERVICE_H
