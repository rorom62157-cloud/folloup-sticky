#pragma once
#include <cstring>
#include <string>
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "http_access.hpp"
#include "note_http.hpp"
#include "web_assets.hpp"

namespace compagnon {
// Transport integration probe, not a complete application. No network setup,
// credential storage, SD mount or display writes. Unsupported private routes
// return 501 rather than a fabricated application state.
class EmbeddedHttp {
public:
    explicit EmbeddedHttp(std::string origin, NoteHttp* notes = nullptr)
        : access_(session_, std::move(origin)), notes_(notes) {
        mutex_ = xSemaphoreCreateMutex();
        notes_mutex_ = xSemaphoreCreateMutex();
    }
    ~EmbeddedHttp() { if (server_) httpd_stop(server_); if (mutex_) vSemaphoreDelete(mutex_); if (notes_mutex_) vSemaphoreDelete(notes_mutex_); }
    EmbeddedHttp(const EmbeddedHttp&) = delete;
    EmbeddedHttp& operator=(const EmbeddedHttp&) = delete;
    esp_err_t begin() {
        if (!mutex_ || !notes_mutex_ || server_) return ESP_ERR_INVALID_STATE;
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.max_req_hdr_len = 2048;
        config.max_uri_len = 256;
        config.max_resp_headers = 12;
        config.max_open_sockets = 3;
        config.lru_purge_enable = true;
        config.recv_wait_timeout = 2;
        config.send_wait_timeout = 2;
        config.stack_size = 8192;
        config.uri_match_fn = httpd_uri_match_wildcard;
        auto result = httpd_start(&server_, &config);
        if (result != ESP_OK) { server_ = nullptr; return result; }
        for (httpd_method_t method : {HTTP_GET, HTTP_POST, HTTP_PATCH, HTTP_DELETE}) {
            httpd_uri_t route{};
            route.uri = "/*"; route.method = method; route.handler = dispatch; route.user_ctx = this;
            result = httpd_register_uri_handler(server_, &route);
            if (result != ESP_OK) { httpd_stop(server_); server_ = nullptr; return result; }
        }
        return ESP_OK;
    }
    // Invoke only from debounced physical commands; never expose as HTTP routes.
    Result physicalDecision(bool accept, std::uint64_t expected_request = 0) {
        if (!lock()) return Result::Busy;
        if (expected_request && (expected_request != pending_generation_ ||
            access_.sessionState(pending_cookie_, now()) != SessionState::Pending)) {
            xSemaphoreGive(mutex_); return Result::InvalidState;
        }
        auto result = accept ? session_.physicalApprove(now()) : session_.physicalReject(now());
        if (!accept && result == Result::Ok) { request_cooldown_until_ = now() + 60000; pending_cookie_.clear(); }
        xSemaphoreGive(mutex_); return result;
    }
    Result revoke() {
        if (!lock()) return Result::Busy;
        auto result = session_.revoke(now()); xSemaphoreGive(mutex_); return result;
    }
    std::uint64_t pendingRequestId() {
        if (!lock()) return 0;
        const auto state = pending_cookie_.empty() ? SessionState::Denied : access_.sessionState(pending_cookie_, now());
        if (state == SessionState::Expired) { request_cooldown_until_ = now() + 60000; pending_cookie_.clear(); }
        const auto result = state == SessionState::Pending ? pending_generation_ : 0;
        xSemaphoreGive(mutex_); return result;
    }
    bool pending() { return pendingRequestId() != 0; }
    bool approved() {
        if (!lock()) return false;
        const bool result = !pending_cookie_.empty() && access_.sessionState(pending_cookie_, now()) == SessionState::Approved;
        xSemaphoreGive(mutex_); return result;
    }
    // Physical UI shares the HTTP store mutex. Never access the borrowed store
    // directly while this server exists. Operation must not call network/UI code.
    template<class Operation> NoteServiceResponse localNotes(Operation operation) {
        if (!notes_ || xSemaphoreTake(notes_mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
            NoteServiceResponse result; result.result = NoteResult::NotReady;
            result.status = 503; result.message = "Stockage occupé ou indisponible.";
            return result;
        }
        auto result = operation(notes_->localService());
        xSemaphoreGive(notes_mutex_);
        return result;
    }
private:
    static Millis now() { return static_cast<Millis>(esp_timer_get_time() / 1000); }
    bool lock() { return mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) == pdTRUE; }
    static std::string capability() {
        unsigned char bytes[16]; esp_fill_random(bytes, sizeof bytes);
        constexpr char hex[] = "0123456789abcdef";
        std::string value; value.reserve(32);
        for (unsigned char byte : bytes) { value += hex[byte >> 4]; value += hex[byte & 15]; }
        return value;
    }
    static std::string header(httpd_req_t* req, const char* name) {
        const auto length = httpd_req_get_hdr_value_len(req, name);
        if (!length || length > 1024) return {};
        std::string value(length + 1, '\0');
        if (httpd_req_get_hdr_value_str(req, name, &value[0], value.size()) != ESP_OK) return {};
        value.resize(length); return value;
    }
    struct Response {
        int code = 200;
        std::string body, cookie;
        const char* asset = nullptr;
        std::size_t asset_size = 0;
        const char* type = "application/json";
        bool close = false;
    };
    static Response error(int code, bool close = false) {
        Response r; r.code = code; r.close = close;
        r.body = code == 501 ?
            R"({"error":"not_implemented","message":"Fonction non raccordée dans ce firmware de test."})" :
            R"({"error":"request_denied","message":"Requête refusée."})";
        return r;
    }
    static esp_err_t send(httpd_req_t* req, const Response& r) {
        const char* status = r.code == 200 ? "200 OK" : r.code == 201 ? "201 Created" :
            r.code == 429 ? "429 Too Many Requests" : r.code == 404 ? "404 Not Found" : r.code == 507 ? "507 Insufficient Storage" : r.code == 202 ? "202 Accepted" :
            r.code == 204 ? "204 No Content" : r.code == 400 ? "400 Bad Request" :
            r.code == 401 ? "401 Unauthorized" : r.code == 403 ? "403 Forbidden" :
            r.code == 409 ? "409 Conflict" : r.code == 413 ? "413 Content Too Large" :
            r.code == 405 ? "405 Method Not Allowed" : r.code == 501 ? "501 Not Implemented" : "503 Service Unavailable";
        httpd_resp_set_status(req, status);
        httpd_resp_set_type(req, r.type);
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; media-src 'self' blob:; object-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
        httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
        if (!r.cookie.empty()) httpd_resp_set_hdr(req, "Set-Cookie", r.cookie.c_str());
        auto result = httpd_resp_send(req, r.asset ? r.asset : r.body.data(), r.asset ? r.asset_size : r.body.size());
        return r.close ? ESP_FAIL : result;
    }
    static esp_err_t dispatch(httpd_req_t* req) {
        auto& self = *static_cast<EmbeddedHttp*>(req->user_ctx);
        if (std::strncmp(req->uri, "/api/notes", 10) == 0) return self.dispatchNotes(req);
        // Receive only the bounded bootstrap body here, without holding UI/session
        // mutex. Authorization is re-evaluated after receive before any mutation.
        bool bootstrap_body = false;
        if (req->method == HTTP_POST && std::strcmp(req->uri, "/api/session/request") == 0 && req->content_len == 2) {
            char body[2]; std::size_t read = 0;
            while (read < sizeof body) {
                int count = httpd_req_recv(req, body + read, sizeof body - read);
                if (count <= 0) return ESP_FAIL;
                read += static_cast<std::size_t>(count);
            }
            bootstrap_body = std::memcmp(body, "{}", 2) == 0;
        }
        if (!self.lock()) return send(req, error(503, req->content_len != 0));
        Response response = self.handle(req, bootstrap_body);
        xSemaphoreGive(self.mutex_);
        // Socket writes may block: never keep the physical-input mutex here.
        return send(req, response);
    }
    HttpAccessDecision authorizeNotes(httpd_req_t* req) {
        for (const char* name : {"Host", "Origin", "Sec-Fetch-Site", "Cookie", "X-CSRF-Token", "Content-Type"})
            if (httpd_req_get_hdr_value_len(req, name) > 1024) return {400};
        const std::string uri(req->uri);
        const char* method = req->method == HTTP_GET ? "GET" : req->method == HTTP_POST ? "POST" :
                             req->method == HTTP_PATCH ? "PATCH" : "DELETE";
        return access_.check({method, uri.substr(0, uri.find('?')), header(req, "Host"),
            header(req, "Origin"), header(req, "Sec-Fetch-Site"), header(req, "Cookie"),
            header(req, "X-CSRF-Token"), req->content_len, header(req, "Content-Type") == "application/json"}, now());
    }
    esp_err_t dispatchNotes(httpd_req_t* req) {
        // Authenticate before receiving private bodies or waiting for storage.
        if (!lock()) return send(req, error(503, req->content_len != 0));
        auto allowed = authorizeNotes(req);
        xSemaphoreGive(mutex_);
        if (!allowed.allowed()) return send(req, error(allowed.status, req->content_len != 0));
        if (!notes_) return send(req, error(501, req->content_len != 0));
        std::string body(req->content_len, '\0');
        std::size_t received = 0;
        while (received < body.size()) {
            const int count = httpd_req_recv(req, &body[received], body.size() - received);
            if (count <= 0) return ESP_FAIL;
            received += static_cast<std::size_t>(count);
        }
        if (xSemaphoreTake(notes_mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return send(req, error(503));
        // Recheck after receive/storage wait. Acceptance here is the operation's
        // authorization point: revocation cannot cancel an already accepted write.
        if (!lock()) { xSemaphoreGive(notes_mutex_); return send(req, error(503)); }
        allowed = authorizeNotes(req);
        xSemaphoreGive(mutex_);
        if (!allowed.allowed()) { xSemaphoreGive(notes_mutex_); return send(req, error(allowed.status)); }
        const std::string uri(req->uri);
        const auto split = uri.find('?');
        const char* method = req->method == HTTP_GET ? "GET" : req->method == HTTP_POST ? "POST" :
                             req->method == HTTP_PATCH ? "PATCH" : "DELETE";
        auto result = notes_->handle(method, uri.substr(0, split),
            split == std::string::npos ? std::string{} : uri.substr(split + 1), body);
        xSemaphoreGive(notes_mutex_);
        if (result.mutated && lock()) {
            access_.acceptedMutation(header(req, "Cookie"), now());
            xSemaphoreGive(mutex_);
        }
        Response response; response.code = result.status; response.body = std::move(result.body);
        return send(req, response);
    }
    Response handle(httpd_req_t* req, bool bootstrap_body) {
        const bool has_body = req->content_len != 0;
        for (const char* name : {"Host", "Origin", "Sec-Fetch-Site", "Cookie", "X-CSRF-Token", "Content-Type"})
            if (httpd_req_get_hdr_value_len(req, name) > 1024) return error(400, has_body);
        const std::string uri(req->uri);
        const auto query = uri.find('?');
        const std::string path = uri.substr(0, query);
        const char* method = req->method == HTTP_GET ? "GET" : req->method == HTTP_POST ? "POST" :
                             req->method == HTTP_PATCH ? "PATCH" : "DELETE";
        const auto cookie = header(req, "Cookie");
        const auto content_type = header(req, "Content-Type");
        HttpAccessRequest request{method, path, header(req, "Host"), header(req, "Origin"),
            header(req, "Sec-Fetch-Site"), cookie, header(req, "X-CSRF-Token"), req->content_len,
            content_type == "application/json"};
        const auto decision = access_.check(request, now());
        if (!decision.allowed()) return error(decision.status, has_body);
        if (req->method == HTTP_POST && path == "/api/session/request") {
            // Query variants deliberately rejected; the body was not consumed.
            if (query != std::string::npos || !bootstrap_body) return error(400, has_body);
            if (!pending_cookie_.empty() && access_.sessionState(pending_cookie_, now()) == SessionState::Expired) {
                request_cooldown_until_ = now() + 60000; pending_cookie_.clear();
            }
            if (now() < request_cooldown_until_) {
                auto response = error(429);
                response.body = R"({"error":"request_cooldown","message":"Patientez une minute après un refus ou une expiration."})";
                return response;
            }
            auto token = capability(); auto csrf = capability();
            auto result = access_.requestSession(token, csrf, now());
            if (result != Result::Ok) return error(409);
            pending_cookie_ = "mc_session=" + token;
            ++pending_generation_; if (!pending_generation_) ++pending_generation_;
            Response response; response.code = 202;
            response.cookie = HttpAccess::cookieHeader(token);
            response.body = R"({"state":"pending"})";
            return response;
        }
        if (req->method == HTTP_GET && path == "/api/session" && !has_body) {
            const auto state = access_.sessionState(cookie, now());
            const char* label = state == SessionState::Approved ? "approved" : state == SessionState::Pending ?
                "pending" : state == SessionState::Expired ? "expired" : "denied";
            Response response;
            response.body = "{\"state\":\"" + std::string(label) + "\"";
            if (state == SessionState::Approved) response.body += ",\"csrf\":\"" + access_.approvedCsrf(cookie, now()) + "\"";
            response.body += "}"; return response;
        }
        if (req->method == HTTP_DELETE && path == "/api/session" && !has_body) {
            session_.revoke(now()); pending_cookie_.clear();
            Response response; response.code = 204;
            response.cookie = "mc_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0";
            return response;
        }
        if (req->method == HTTP_GET && !has_body) {
            for (const auto& asset : web_assets) if (path == asset.path || (path == "/" && std::strcmp(asset.path, "/index.html") == 0)) {
                Response response; response.asset = asset.data; response.asset_size = asset.size; response.type = asset.type;
                return response;
            }
        }
        return error(501, has_body);
    }
    CompanionSession session_;
    HttpAccess access_;
    NoteHttp* notes_ = nullptr; // Lifetime exceeds server; exclusive store ownership while active.
    SemaphoreHandle_t notes_mutex_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    httpd_handle_t server_ = nullptr;
    std::string pending_cookie_;
    std::uint64_t pending_generation_ = 0;
    Millis request_cooldown_until_ = 0;
};
} // namespace compagnon
