#include "http_access.hpp"
#include <cassert>
#include <iostream>
using namespace compagnon;
int main() {
    CompanionSession session;
    HttpAccess gate(session, "http://192.168.4.1");
    const std::string token(32, 'a'), csrf(32, 'b');
    const auto cookie = "mc_session=" + token;
    HttpAccessRequest r{"GET", "/api/state", "192.168.4.1", "", "same-origin", cookie, "", 0, false};
    assert(gate.check(r, 0).status == 401);
    assert(gate.requestSession(token, csrf, 1) == Result::Ok);
    assert(gate.approvedCsrf(cookie, 2).empty());
    assert(gate.check(r, 2).status == 401);
    assert(session.physicalApprove(3) == Result::Ok);
    assert(gate.check(r, 4).allowed());
    assert(gate.approvedCsrf(cookie, 4) == csrf);
    r.cookie = "mc_session=" + std::string(32, 'c');
    assert(gate.check(r, 5).status == 401);
    r.cookie = cookie + "; " + cookie;
    assert(gate.check(r, 6).status == 401);
    r.cookie = cookie;
    r.method = "POST"; r.path = "/api/notes"; r.origin = "http://192.168.4.1"; r.json_content = true;
    assert(gate.check(r, 7).status == 403);
    r.csrf = csrf;
    assert(gate.check(r, 8).allowed());
    r.origin = "https://attacker.example";
    assert(gate.check(r, 9).status == 403);
    r.origin.clear(); assert(gate.check(r, 10).status == 403);
    r.origin = "http://192.168.4.1";
    r.fetch_site = "cross-site"; assert(gate.check(r, 11).status == 403);
    r.fetch_site = "same-site"; assert(gate.check(r, 12).status == 403);
    r.fetch_site = "same-origin";
    r.host = "attacker.example"; assert(gate.check(r, 13).status == 403);
    r.host = "192.168.4.1";
    r.body_bytes = 24577; assert(gate.check(r, 14).status == 413);
    r.body_bytes = 0; r.method = "DELETE"; r.json_content = false;
    assert(gate.check(r, 15).allowed()); // Actual client sends DELETE without body.
    r.method = "GET"; r.path = "/audio/../../secrets";
    assert(gate.check(r, 16).status == 400);
    r.path = "/audio/%2e%2e/secrets"; assert(gate.check(r, 17).status == 400);
    r.path = "/api/state";
    // Reading/polling must never extend the inactivity deadline.
    assert(gate.check(r, 600002).allowed());
    assert(gate.check(r, 600003).status == 401);
    r.path = "/sd/secret.txt"; assert(gate.check(r, 600004).status == 401);
    r.path = "/api/settings/gemini"; assert(gate.check(r, 600005).status == 401);
    r.path = "/app.js"; assert(gate.check(r, 600006).allowed());
    r.method = "POST"; r.path = "/api/session/request"; r.json_content = true;
    assert(gate.check(r, 600007).allowed());
    assert(gate.requestSession(std::string(32, 'c'), std::string(32, 'd'), 600008) == Result::Ok);
    assert(session.physicalApprove(600009) == Result::Ok);
    r.cookie = "mc_session=" + std::string(32, 'c'); r.csrf = csrf;
    r.path = "/api/notes";
    assert(gate.check(r, 600010).status == 403); // Old CSRF cannot cross sessions.
    assert(session.revoke(600011) == Result::Ok);
    assert(gate.approvedCsrf(r.cookie, 600012).empty());
    assert(HttpAccess::cookieHeader("bad\r\nheader").empty());
    HttpAccess badGate(session, "http://192.168.4.1/evil");
    assert(badGate.check(r, 600013).status == 403);
    std::cout << "HTTP access boundary tests passed\n";
}
