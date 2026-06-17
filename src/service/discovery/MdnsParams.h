#ifndef SERVICE_MDNS_PARAMS_H
#define SERVICE_MDNS_PARAMS_H

#include "MdnsService.h"

#include <cstdint>
#include <memory>
#include <string>

class DeviceConfig;  // forward declaration (real include in the .cpp)

namespace service {

// Build MdnsServiceParams from DeviceConfig + interface/IP/ports.
// (Verbatim move of main_app.cpp buildMdnsParams; file-local
//  trimConfigString/getDefaultMdnsInstanceName/getDefaultMdnsHostName
//  move with it as file-local helpers in MdnsParams.cpp.)
MdnsServiceParams buildMdnsParams(const std::shared_ptr<DeviceConfig>& config,
                                  const std::string& interface_name,
                                  const std::string& ip_address,
                                  uint16_t ctrl_port,
                                  uint16_t rtsp_port);

bool isMdnsEnabled(const std::shared_ptr<DeviceConfig>& config);

} // namespace service

#endif // SERVICE_MDNS_PARAMS_H
