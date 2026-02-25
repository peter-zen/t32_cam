#include <iostream>
#include <thread>
#include "../src/service/camera/CameraServiceFactory.h"
#include <elog.h>

int main() {
    // Initialize logger (console only)
    elog_init();
    elog_set_fmt(ELOG_LVL_ALL, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_start();

    std::cout << "Testing CameraService Abstraction..." << std::endl;

    auto cameraService = service::CameraServiceFactory::create();
    if (!cameraService) {
        std::cerr << "Failed to create CameraService" << std::endl;
        return 1;
    }

    std::cout << "CameraService created." << std::endl;

    // Test Properties
    std::cout << "Properties: " << cameraService->getAllPropertiesJson() << std::endl;

    // Test Photo
    std::cout << "Taking photo..." << std::endl;
    service::PhotoResult result;
    int ret = cameraService->takePhoto(0, true, "jpg", 90, result);
    if (ret == 0) {
        std::cout << "Photo success: " << result.filePath << std::endl;
    } else {
        std::cerr << "Photo failed" << std::endl;
    }

    // Test Record
    std::cout << "Starting record..." << std::endl;
    ret = cameraService->startRecord(0, 10, true, "test_job");
    if (ret == 0) {
        std::cout << "Record started." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        cameraService->stopRecord();
        std::cout << "Record stopped." << std::endl;
    } else {
        std::cerr << "Record failed" << std::endl;
    }

    std::cout << "Test finished." << std::endl;
    return 0;
}
