#pragma once
#include "compagnon.hpp"
#include <string>

namespace compagnon {
// LocalPassword is an explicit application policy for a password-authenticated local AP.
// Physical remains the default for all existing callers.
enum class PairingMode { Physical, LocalPassword };
struct HttpAccessRequest {
    std::string method, path, host, origin, fetch_site, cookie, csrf;
    std::size_t body_bytes = 0;
    bool json_content = false;
};
struct HttpAccessDecision {
    int status;
    bool allowed() const { return status == 200; }
};
// One instance per server; caller serializes access with CompanionSession.
// Adapter must bound headers/body BEFORE allocating buffers and must not expose
// private files through a second static-file handler. No CORS is permitted.
class HttpAccess {
public:
    HttpAccess(CompanionSession& session, std::string trusted_origin,
               PairingMode mode = PairingMode::Physical, bool local_password_authenticated = false);
    PairingMode pairingMode() const { return mode_; }
    bool validPolicy() const { return mode_ == PairingMode::Physical || local_password_authenticated_; }
    // Called only after cryptographic generation (32 lowercase hex characters).
    Result requestSession(const std::string& token, const std::string& csrf, Millis now);
    HttpAccessDecision check(const HttpAccessRequest& request, Millis now);
    SessionState sessionState(const std::string& cookie, Millis now);
    std::string approvedCsrf(const std::string& cookie, Millis now);
    Result acceptedMutation(const std::string& cookie, Millis now);
    static std::string cookieHeader(const std::string& token);
private:
    static bool validCapability(const std::string& value);
    static std::string tokenFromCookie(const std::string& cookie);
    CompanionSession& session_;
    PairingMode mode_;
    bool local_password_authenticated_;
    std::string origin_, host_, csrf_;
};
} // namespace compagnon
