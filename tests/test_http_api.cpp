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
//   H1 (T28):        GET /api/v1/device/capabilities -> 200 + body has "capabilities"
//   H2/H3 (T28):     lean-fixture child: POST /api/v1/camera/photo, /video/start -> 404
//                    (routes not registered when um_snap/um_rec absent; design §3.9)
//   H4 (loopback #1):lean-fixture child: GET /api/v1/camera/photos -> 404
//                    (photos reads existing media = um_pb gate; design §3.6)
//
// http_server.c uses a file-static singleton, so exactly one server per binary.
// The main process boots against the all-open product_sample.json fixture so the
// playback_token path (um_pb) still resolves. H2/H3 fork+exec THIS binary with
// a lean ({um_live}-only) fixture so the gate actually fires.
// Exit: 0 = all pass, non-zero = failure. Build under BUILD_FOR_SIMULATION only.

#include "http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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

// H1 — GET /api/v1/device/capabilities returns 200 + JSON array (design §3.8).
// Runs against the all-open fixture so the array has all 4 tokens.
void testCapabilitiesEndpoint()
{
    auto r = httpRequest("GET", "/api/v1/device/capabilities");
    EXPECT_TRUE(r.connected, "/capabilities connected");
    EXPECT_EQ(r.status, 200, "/capabilities -> 200");
    EXPECT_TRUE(r.body.find("capabilities") != std::string::npos,
                "/capabilities body has 'capabilities' key");
    EXPECT_TRUE(r.body.find("um_live") != std::string::npos,
                "/capabilities body has um_live token");
}

}  // namespace

// H2/H3 — lean-fixture child: product.json with only {um_live}. snap/rec/pb
// routes must NOT be registered -> POST/GET return 404 (design §3.9). This is
// re-exec'd from main() so the http_server singleton loads the lean caps set.
int runLeanCapsChild()
{
    elog_init();
    elog_start();

    HttpServerConfig cfg{kPort, nullptr, 2};
    if (http_server_init(&cfg) != 0) return 1;
    if (http_server_start() != 0) { http_server_deinit(); return 1; }
    usleep(800 * 1000);

    int fail = 0;
    // H2 — um_snap absent -> POST /api/v1/camera/photo -> 404 (route not registered)
    {
        auto r = httpRequest("POST", "/api/v1/camera/photo");
        if (!r.connected) { std::fprintf(stderr, "FAIL H2: photo not connected\n"); ++fail; }
        if (r.status != 404) { std::fprintf(stderr, "FAIL H2: photo -> %d, want 404\n", r.status); ++fail; }
    }
    // H3 — um_rec absent -> POST /api/v1/camera/video/start -> 404
    {
        auto r = httpRequest("POST", "/api/v1/camera/video/start");
        if (!r.connected) { std::fprintf(stderr, "FAIL H3: video/start not connected\n"); ++fail; }
        if (r.status != 404) { std::fprintf(stderr, "FAIL H3: video/start -> %d, want 404\n", r.status); ++fail; }
    }
    // H4 (loopback #1) — um_pb absent -> GET /api/v1/camera/photos -> 404.
    // photos reads existing media (browse), moved from um_snap to um_pb gate
    // (design §3.6: read-existing = um_pb). Lean fixture has no um_pb -> 404.
    {
        auto r = httpRequest("GET", "/api/v1/camera/photos");
        if (!r.connected) { std::fprintf(stderr, "FAIL H4: photos not connected\n"); ++fail; }
        if (r.status != 404) { std::fprintf(stderr, "FAIL H4: photos -> %d, want 404\n", r.status); ++fail; }
    }
    // Sanity: /capabilities still works + um_live present
    {
        auto r = httpRequest("GET", "/api/v1/device/capabilities");
        if (r.status != 200 || r.body.find("um_live") == std::string::npos) {
            std::fprintf(stderr, "FAIL: lean /capabilities wrong\n");
            ++fail;
        }
    }
    // Sanity: control route still present (um_live doesn't gate /device/info)
    {
        auto r = httpRequest("GET", "/api/v1/device/info");
        if (r.status != 200) { std::fprintf(stderr, "FAIL: lean /device/info -> %d\n", r.status); ++fail; }
    }

    http_server_stop();
    http_server_deinit();
    return fail;
}

int main(int argc, char* argv[])
{
    // H2/H3 child mode: PRODUCT_FILE is set by the parent before exec.
    if (argc == 2 && std::strcmp(argv[1], "--lean-caps") == 0) {
        return runLeanCapsChild();
    }

    elog_init();
    elog_start();

    // T28 — set PRODUCT_FILE to the all-open fixture so the http_api_register_v1
    // gate loads {um_live,um_snap,um_rec,um_pb} and the existing playback_token
    // path (um_pb-gated) still resolves.
    const char* productEnv = std::getenv("PRODUCT_FILE");
    if (!productEnv || productEnv[0] == '\0') {
        // Resolve tests/assets/configs/product_sample.json relative to CWD
        // (build_sim/bin). TEST_PROJECT_ROOT is not defined here; fall back to a
        // path search.
        std::string candidates[] = {
            "./../../tests/assets/configs/product_sample.json",
            "./tests/assets/configs/product_sample.json",
            "./../tests/assets/configs/product_sample.json",
        };
        for (const auto& c : candidates) {
            std::ifstream f(c.c_str());
            if (f.is_open()) {
                ::setenv("PRODUCT_FILE", c.c_str(), 1);
                break;
            }
        }
    }

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
    testCapabilitiesEndpoint();

    http_server_stop();
    http_server_deinit();

    // H2/H3 — fork+exec lean-fixture child. Write the lean product.json, set
    // PRODUCT_FILE, exec self with --lean-caps. http_server singleton is fresh
    // in the child so it loads the lean caps set at registration time.
    {
        char tmpBuf[] = "/tmp/t28_lean_product_XXXXXX";
        int fd = ::mkstemp(tmpBuf);
        if (fd < 0) {
            std::fprintf(stderr, "FAIL: mkstemp lean fixture\n");
            ++g_failures;
        } else {
            const char* leanJson =
                "{\n"
                "    \"BOOT\": { \"PModel\": \"T32\" },\n"
                "    \"capabilities\": \"um_live\"\n"
                "}\n";
            ssize_t wr = ::write(fd, leanJson, std::strlen(leanJson));
            (void)wr;
            ::close(fd);
            ::setenv("PRODUCT_FILE", tmpBuf, 1);

            char selfBuf[4096];
            ssize_t selfLen = ::readlink("/proc/self/exe", selfBuf, sizeof(selfBuf) - 1);
            std::string self = (selfLen > 0) ? std::string(selfBuf, static_cast<size_t>(selfLen))
                                              : std::string(argv[0]);
            pid_t pid = ::fork();
            if (pid < 0) {
                std::fprintf(stderr, "FAIL: fork lean child\n");
                ++g_failures;
            } else if (pid == 0) {
                ::execl(self.c_str(), self.c_str(), "--lean-caps", (char*)nullptr);
                ::_exit(127);
            } else {
                int status = 0;
                ::waitpid(pid, &status, 0);
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    std::fprintf(stderr, "FAIL: lean caps child exit %d\n",
                                 WIFEXITED(status) ? WEXITSTATUS(status) : -1);
                    ++g_failures;
                }
            }
            ::unlink(tmpBuf);
        }
    }

    if (g_failures == 0) {
        std::printf("test_http_api: ALL PASS\n");
        return 0;
    }
    std::fprintf(stderr, "test_http_api: %d FAILURE(S)\n", g_failures);
    return 1;
}
