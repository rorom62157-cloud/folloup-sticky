#include "http_access.hpp"
#include <utility>

namespace compagnon {
namespace {
bool equalSecret(const std::string& a, const std::string& b) {
    if (a.empty() || a.size() != b.size()) return false;
    unsigned diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}
}
HttpAccess::HttpAccess(CompanionSession& session, std::string origin)
    : session_(session), origin_(std::move(origin)) {
    const auto prefix = origin_.rfind("http://", 0) == 0 ? 7U :
                        origin_.rfind("https://", 0) == 0 ? 8U : 0U;
    if (prefix && origin_.find_first_of("/?#@\r\n", prefix) == std::string::npos)
        host_ = origin_.substr(prefix);
}
bool HttpAccess::validCapability(const std::string& value) {
    if (value.size() != 32) return false;
    for (const char c : value) if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
    return true;
}
std::string HttpAccess::tokenFromCookie(const std::string& cookie) {
    if (cookie.size() > 1024) return {};
    std::string found;
    std::size_t start = 0;
    while (start < cookie.size()) {
        const auto end = cookie.find(';', start);
        const auto itemEnd = end == std::string::npos ? cookie.size() : end;
        while (start < itemEnd && cookie[start] == ' ') ++start;
        const auto item = cookie.substr(start, itemEnd - start);
        if (item.rfind("mc_session=", 0) == 0) {
            if (!found.empty()) return {}; // Reject ambiguous duplicate cookies.
            found = item.substr(11);
            if (!validCapability(found)) return {};
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return found;
}
Result HttpAccess::requestSession(const std::string& token, const std::string& csrf, Millis now) {
    if (!validCapability(token) || !validCapability(csrf) || token == csrf) return Result::InvalidArgument;
    const auto result = session_.request(token, now);
    if (result == Result::Ok) csrf_ = csrf;
    return result;
}
HttpAccessDecision HttpAccess::check(const HttpAccessRequest& r, Millis now) {
    if (host_.empty() || r.host != host_) return {403}; // DNS rebinding / foreign Host.
    if (r.path.empty() || r.path.size() > 256 || r.path[0] != '/' ||
        r.path.find_first_of("%\\?#\r\n") != std::string::npos ||
        r.path.find("..") != std::string::npos) return {400};
    if (r.body_bytes > 24576) return {413};
    if ((!r.fetch_site.empty() && r.fetch_site != "same-origin" && r.fetch_site != "none") ||
        (!r.origin.empty() && r.origin != origin_)) return {403};
    const bool get = r.method == "GET";
    const bool mutation = r.method == "POST" || r.method == "PATCH" || r.method == "DELETE";
    if (!get && !mutation) return {405};
    // Exact Origin is mandatory for writes, including bootstrap; JSON prevents
    // cross-site simple form submissions on browsers without Fetch Metadata.
    if (mutation && (r.origin != origin_ ||
        ((r.method != "DELETE" || r.body_bytes != 0) && !r.json_content))) return {403};
    if (get && (r.path == "/" || r.path == "/index.html" || r.path == "/app.js" || r.path == "/style.css" || r.path == "/api/session")) return {200};
    if (r.method == "POST" && r.path == "/api/session/request") return {200};
    if (!session_.authorizePrivate(tokenFromCookie(r.cookie), now)) return {401};
    if (mutation && !equalSecret(r.csrf, csrf_)) return {403};
    // Handler dispatch still checks supported routes and returns 404/405.
    // Every unknown path is private by default, including audio/static SD paths.
    return {200};
}
SessionState HttpAccess::sessionState(const std::string& cookie, Millis now) {
    return session_.status(tokenFromCookie(cookie), now);
}
std::string HttpAccess::approvedCsrf(const std::string& cookie, Millis now) {
    return session_.authorizePrivate(tokenFromCookie(cookie), now) ? csrf_ : std::string{};
}
Result HttpAccess::acceptedMutation(const std::string& cookie, Millis now) {
    return session_.acceptedUserActivity(tokenFromCookie(cookie), now);
}
std::string HttpAccess::cookieHeader(const std::string& token) {
    if (!validCapability(token)) return {};
    return "mc_session=" + token + "; HttpOnly; SameSite=Strict; Path=/";
}
} // namespace compagnon
