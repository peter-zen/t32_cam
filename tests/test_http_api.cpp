// HTTP control-endpoint + playback_token unit test (manifest §B6, module 7).
//
// The existing sim tests only cover camera *properties*; the control endpoints
// and the playback_token resolution path are untested. Handlers in
// http_api_v1.cpp are `static`, so the only testable idiom is to bring the real
// CivetWeb server up in-process on a localhost port and issue HTTP requests
// against it (http_server lib already builds under sim).
//
// Covers (no on-disk media DB needed for any of these):
//   - probes:        GET /api/health, /healthz  -> 200
//   - control:       GET /api/v1/device/info, /api/v1/camera/status -> 200
//   - method gate:   POST /api/v1/device/info   -> 405
//   - playback_token:GET /api/v1/camera/video/playback?token=bogus -> 404
//                    (resolve_media_item -> dao.getMediaByPlaybackToken finds
//                     nothing -> 404; this is the manifest's key untested path)
//                    POST .../playback           -> 405
//
// A positive playback (200 with a real token+file) needs a seeded media DB and
// is intentionally left out (documented stretch — see plan §B6).
//
// http_server.c uses a file-static singleton, so exactly one server per binary.
// Exit: 0 = all pass, non-zero = failure. Build under BUILD_FOR_SIMULATION only.

#include "http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <elog.h>

namespace {

int g_failures = 0;
const int kPort = 18080;
const char* kHost = "127.0.0.1";

#define EXPECT_TRUE(x, msg)                                                    \
    do {                                                                       \
        if (!(x)) {                                                            \
            std::fprintf(stderr, "FAIL [%s]: line %d: %s is false\n",          \
                         msg, __LINE__, #x);                                   \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

#define EXPECT_EQ(actual, expected, msg)                                       \
    do {                                                                       \
        int _a = (actual);                                                     \
        int _e = (expected);                                                   \
        if (_a != _e) {                                                        \
            std::fprintf(stderr, "FAIL [%s]: line %d: got %d, expected %d\n",  \
                         msg, __LINE__, _a, _e);                               \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

struct HttpResponse {
    bool connected = false;  // TCP connect + request send succeeded
    int status = 0;          // HTTP status code (0 if unparseable)
    std::string body;
};

// Minimal blocking HTTP client with a 3s socket timeout so a missing route
// can never hang the test. method = "GET" / "POST".
HttpResponse httpRequest(const char* method, const std::string& path)
{
    HttpResponse r;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return r;

    timeval to{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(to));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    inet_pton(AF_INET, kHost, &addr.sin_addr);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return r;
    }

    std::string req = std::string(method) + " " + path +
        " HTTP/1.1\r\nHost: " + kHost +
        "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    if (send(fd, req.c_str(), req.size(), 0) < 0) {
        close(fd);
        return r;
    }
    r.connected = true;

    std::string resp;
    char buf[4096];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        resp.append(buf, static_cast<size_t>(n));
    }
    close(fd);

    // "HTTP/1.1 <status> ..."
    size_t sp = resp.find(' ');
    if (sp != std::string::npos) r.status = std::atoi(resp.c_str() + sp + 1);
    size_t sep = resp.find("\r\n\r\n");
    r.body = (sep != std::string::npos) ? resp.substr(sep + 4) : resp;
    return r;
}

void testHealthProbes()
{
    auto h = httpRequest("GET", "/api/health");
    EXPECT_TRUE(h.connected, "/api/health connected");
    EXPECT_EQ(h.status, 200, "/api/health -> 200");
    EXPECT_TRUE(h.body.find("ok") != std::string::npos, "/api/health body has 'ok'");

    auto z = httpRequest("GET", "/healthz");
    EXPECT_TRUE(z.connected, "/healthz connected");
    EXPECT_EQ(z.status, 200, "/healthz -> 200");
}

void testControlEndpoints()
{
    auto d = httpRequest("GET", "/api/v1/device/info");
    EXPECT_TRUE(d.connected, "/api/v1/device/info connected");
    EXPECT_EQ(d.status, 200, "/api/v1/device/info -> 200");

    auto s = httpRequest("GET", "/api/v1/camera/status");
    EXPECT_TRUE(s.connected, "/api/v1/camera/status connected");
    EXPECT_EQ(s.status, 200, "/api/v1/camera/status -> 200");
}

void testMethodGate()
{
    // Control endpoints are GET-only; POST must be rejected with 405 (not 200,
    // not 500). Locks the method-validation contract shared by all v1 handlers.
    auto p = httpRequest("POST", "/api/v1/device/info");
    EXPECT_TRUE(p.connected, "POST /api/v1/device/info connected");
    EXPECT_EQ(p.status, 405, "POST /api/v1/device/info -> 405");
}

void testPlaybackTokenResolution()
{
    // Bogus token -> dao.getMediaByPlaybackToken finds nothing -> 404. This is
    // the playback_token code path (resolve_media_item, http_api_v1.cpp:256)
    // that no test previously exercised; a clean 404 (not a crash/500) proves
    // the resolver + MetadataDao degrade safely with no seeded media DB.
    auto b = httpRequest("GET", "/api/v1/camera/video/playback?token=bogus");
    EXPECT_TRUE(b.connected, "playback?token=bogus connected");
    EXPECT_EQ(b.status, 404, "playback?token=bogus -> 404 (not found)");

    // No token/id at all -> 400 (resolve_media_item "Missing media id or token").
    auto m = httpRequest("GET", "/api/v1/camera/video/playback");
    EXPECT_TRUE(m.connected, "playback (no args) connected");
    EXPECT_EQ(m.status, 400, "playback (no args) -> 400");

    // Wrong method -> 405 (method gate fires before resolution).
    auto post = httpRequest("POST", "/api/v1/camera/video/playback?token=bogus");
    EXPECT_TRUE(post.connected, "POST playback connected");
    EXPECT_EQ(post.status, 405, "POST playback -> 405");
}

}  // namespace

int main()
{
    elog_init();
    elog_start();

    HttpServerConfig cfg{kPort, nullptr, 2};
    if (http_server_init(&cfg) != 0) {
        std::fprintf(stderr, "FAIL: http_server_init returned -1\n");
        return 1;
    }
    if (http_server_start() != 0) {
        std::fprintf(stderr, "FAIL: http_server_start returned -1\n");
        http_server_deinit();
        return 1;
    }
    // Give CivetWeb a moment to bind before we connect.
    usleep(800 * 1000);

    testHealthProbes();
    testControlEndpoints();
    testMethodGate();
    testPlaybackTokenResolution();

    http_server_stop();
    http_server_deinit();

    if (g_failures == 0) {
        std::printf("test_http_api: ALL PASS\n");
        return 0;
    }
    std::fprintf(stderr, "test_http_api: %d FAILURE(S)\n", g_failures);
    return 1;
}
