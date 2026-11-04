// SPDX-License-Identifier: GPL-3.0-only
#include "http_self_test.hpp"

#include <algorithm>
#include <cstring>
#include "esp_http_client.h"
#include "esp_timer.h"
#include "web_assets.hpp"

namespace compagnon::device {
namespace {
struct CheckResult {
    int status = 0;
    std::size_t bytes = 0;
    esp_err_t error = ESP_OK;
    bool complete = false;
};
CheckResult get(const char* path, const WebAsset* expected) {
    CheckResult result;
    const std::string url = std::string("http://127.0.0.1") + path;
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = 750;
    config.buffer_size = 1024;
    config.buffer_size_tx = 512;
    auto client = esp_http_client_init(&config);
    if (!client) { result.error = ESP_ERR_NO_MEM; return result; }
    const auto start = esp_timer_get_time();
    // Use the trusted AP Host even though transport stays on loopback. No Cookie,
    // Authorization, Origin or POST: the check cannot acquire/replace a session.
    result.error = esp_http_client_set_header(client, "Host", "192.168.4.1");
    if (result.error == ESP_OK) result.error = esp_http_client_set_header(client, "Connection", "close");
    if (result.error == ESP_OK) result.error = esp_http_client_open(client, 0);
    bool matches = true;
    if (result.error == ESP_OK) {
        const auto length = esp_http_client_fetch_headers(client);
        result.status = esp_http_client_get_status_code(client);
        if (length < 0) result.error = ESP_FAIL;
        const std::size_t limit = expected ? std::min<std::size_t>(expected->size, 256 * 1024) : 4096;
        if (length > static_cast<int64_t>(limit)) result.error = ESP_ERR_INVALID_SIZE;
        char buffer[1024];
        while (result.error == ESP_OK && !esp_http_client_is_complete_data_received(client)) {
            if (esp_timer_get_time() - start >= 3000000LL) {
                result.error = ESP_ERR_TIMEOUT; break;
            }
            const int count = esp_http_client_read(client, buffer, sizeof(buffer));
            if (count < 0) { result.error = ESP_FAIL; break; }
            if (!count) {
                if (!esp_http_client_is_complete_data_received(client)) result.error = ESP_FAIL;
                break;
            }
            const auto size = static_cast<std::size_t>(count);
            if (size > limit - result.bytes) { result.error = ESP_ERR_INVALID_SIZE; break; }
            if (expected && std::memcmp(buffer, expected->data + result.bytes, size) != 0) matches = false;
            result.bytes += size;
        }
        result.complete = result.error == ESP_OK && esp_http_client_is_complete_data_received(client) &&
            result.status == 200 && matches && (!expected || result.bytes == expected->size);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}
}  // namespace

bool runHttpSelfTest(std::string& summary) {
    summary = "HTTP boucle locale";
    bool passed = true;
    for (const auto& asset : web_assets) {
        const char* path = std::strcmp(asset.path, "/index.html") == 0 ? "/" : asset.path;
        const auto result = get(path, &asset);
        passed = passed && result.complete;
        summary += " | " + std::string(path) + " status=" + std::to_string(result.status) +
            " bytes=" + std::to_string(result.bytes) + "/" + std::to_string(asset.size) +
            " result=" + (result.complete ? "OK" : esp_err_to_name(result.error == ESP_OK ? ESP_FAIL : result.error));
    }
    const auto session = get("/api/session", nullptr);
    passed = passed && session.complete;
    summary += " | /api/session status=" + std::to_string(session.status) +
        " bytes=" + std::to_string(session.bytes) + " result=" +
        (session.complete ? "OK" : esp_err_to_name(session.error == ESP_OK ? ESP_FAIL : session.error));
    return passed;
}
}  // namespace compagnon::device
