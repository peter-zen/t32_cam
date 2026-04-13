#include "TcpEventService.h"

#include <elog.h>
#include <json/json.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

namespace {

uint16_t findFreePort() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return 0;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        close(fd);
        return 0;
    }

    const uint16_t port = ntohs(addr.sin_port);
    close(fd);
    return port;
}

int connectClient(uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

bool readLineWithTimeout(int fd, std::string& line, int timeoutSeconds) {
    line.clear();

    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        timeval timeout;
        timeout.tv_sec = timeoutSeconds;
        timeout.tv_usec = 0;

        const int ready = select(fd + 1, &readfds, NULL, NULL, &timeout);
        if (ready <= 0) {
            return false;
        }

        char buffer[256];
        const ssize_t bytes = recv(fd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            return false;
        }

        line.append(buffer, static_cast<size_t>(bytes));
        const std::string::size_type newlinePos = line.find('\n');
        if (newlinePos != std::string::npos) {
            line.resize(newlinePos);
            return true;
        }
    }
}

bool parseJson(const std::string& text, Json::Value& root) {
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream iss(text);
    return Json::parseFromStream(builder, iss, &root, &errors);
}

} // namespace

int main() {
    elog_init();
    elog_start();

    const uint16_t port = findFreePort();
    if (port == 0) {
        std::cerr << "Failed to find free port" << std::endl;
        return EXIT_FAILURE;
    }

    std::shared_ptr<service::TcpEventService> eventService = service::TcpEventService::getInstance();
    if (!eventService->start(port)) {
        std::cerr << "Failed to start TcpEventService on port " << port << std::endl;
        return EXIT_FAILURE;
    }

    const int clientFd = connectClient(port);
    if (clientFd < 0) {
        std::cerr << "Failed to connect test client: errno=" << errno << std::endl;
        eventService->stop();
        return EXIT_FAILURE;
    }

    std::string line;
    if (!readLineWithTimeout(clientFd, line, 3)) {
        std::cerr << "Timed out waiting for heartbeat" << std::endl;
        close(clientFd);
        eventService->stop();
        return EXIT_FAILURE;
    }

    Json::Value root;
    if (!parseJson(line, root)) {
        std::cerr << "Failed to parse heartbeat JSON: " << line << std::endl;
        close(clientFd);
        eventService->stop();
        return EXIT_FAILURE;
    }

    bool ok = true;
    ok = ok && root["category"].asString() == "device";
    ok = ok && root["type"].asString() == "device.heartbeat";
    ok = ok && root["level"].asString() == "info";
    ok = ok && root["sequence"].asUInt64() == 1;
    ok = ok && root["data"]["heartbeat_interval_ms"].asUInt64() == ::service::kDefaultTcpHeartbeatIntervalMs;

    close(clientFd);
    eventService->stop();

    if (!ok) {
        std::cerr << "Unexpected heartbeat payload: " << line << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
