# Contracts — first implementation slice

Status: frozen for parallel core/web work, 2026-11-04. This slice implements real host-testable domain logic and companion UI; firmware adapters follow. Do not claim device firmware integration before it exists.

## Core ownership
core/ contains C++17 headers/sources independent of SDK. Public namespace compagnon. Every time is monotonic uint64_t milliseconds passed by caller. Wall-clock event time is separate. Core called under one owner/task (not thread-safe by itself).

Application domain: SleepPolicy with configurable allowed light/deep durations (30s/600s defaults, deep 0=never), only userActivity resets inactivity, blockers prevent entry, automatic light retains content, manual light chooses clock unless post-it, deep selects post-it or brand. No background Google sync; physical wake signals request sync. RecordingWorkflow tracks hold/long capture, 240000ms cap, save success before menus, type then contextual action, no selection retains pending, destructive delete confirmed. Save failure stays recoverable. No DSP/audio implementation claimed.

Authorization: CompanionSession stores one pending and one active opaque externally-generated token. No random generation in pure core; adapter uses cryptographic entropy. Request lifetime 60s, inactivity expiry 10min; physical approval only, rejection, expiry/revoke deny all private operations. Pending request cannot replace active/pending session. No private GET before approval. Every HTTP adapter must enforce authorization, origin/CSRF protection and bounds; domain manager alone is not network security.

## Embedded web API v1
JSON responses. No secrets returned. Pending/active session is cookie-bound in future adapter (HttpOnly SameSite=Strict path=/, unpredictable >=128bit). Fetch same-origin credentials only. CSRF token provided only after approved session. A simulator is explicitly marked, never presented as actual device.
- POST /api/session/request {} -> 202 {state:"pending"}; 409 busy; bootstrap cookie set by adapter, no personal data.
- GET /api/session -> {state:"pending"|"approved"|"denied"|"expired", csrf?:string}; no device data unless approved.
- DELETE /api/session with X-CSRF-Token -> 204.
- GET /api/state -> {name:"Mon compagnon",battery_percent:null|number,network:{internet:boolean,ssid:string},sleep:"awake"|"light",google:{state:"not_configured"|"pending"|"ready"|"error"},gemini:{configured:boolean},notes:[{id,title,text,pinned,audio_id?:string,version:number}],recordings:[{id,title,category,status}],agenda:[{id,title,start}],storage:{free_bytes,total_bytes}}. All data require approved session.
- POST /api/notes {title,text} -> 201 note.
- PATCH /api/notes/:id {title,text,pinned,version} -> 200 updated note, 409 revision conflict.
- DELETE /api/notes/:id with version query -> 204 or409.
- GET /api/recordings/:id/audio -> authenticated file only.
- POST /api/settings/gemini {api_key} -> 204; never echo key.
- POST /api/settings/wifi {ssid,password} -> 202; never echo password.
Errors {error:"code",message:"French explanation"}; 401 expired/not approved returns locked UI. Read-only poll does not extend active user inactivity. Explicit accepted mutation may extend session.

## Prototype boundaries
Web owns web/ only; vanilla HTML/CSS/JS no build dependencies. Core owns core/ and tests/ only. Root owns scripts/, docs/, firmware integration. UI must not fake success for unimplemented operations. Host test runner will compile with clang++ -std=c++17 -Wall -Wextra -Werror -pedantic; source/test locations reported by agent.
