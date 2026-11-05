# ADR 002 — Google manual provisioning without personal backend

Research conclusion, 2026-11-04. Manual developer provisioning selected for the personal prototype; not yet implemented or tested with credentials.

## Constraint
Gemini key and Google Calendar/Tasks OAuth are independent. Local HTTP .local or LAN-IP companion cannot itself use Google's documented web-origin/redirect flows. Device flow does not list Calendar/Tasks among allowed scopes. A local page cannot bypass these restrictions.

## Feasible personal provisioning route
Official OAuth Playground supports personal Web application credentials with redirect https://developers.google.com/oauthplayground, Use your own OAuth credentials, Offline access and refresh-token exchange. Authorize Calendar/Tasks in Playground; provision client_id, client_secret and refresh token to the device; device can renew an access token over verified HTTPS and call Google directly. No personal backend or native companion is mandatory for this manual setup.

Applying the documented token mechanics to an ESP is a technical inference, not an ESP-certified Google deployment. Consumer-grade onboarding remains out of scope. The first research conclusion was too restrictive and was corrected by this targeted follow-up.

Default Playground credentials have 24-hour token revocation; own credentials are required. External Testing Calendar/Tasks refresh tokens expire after seven days; document production/personal-use setup and recovery after invalid_grant/revocation. Tokens are never guaranteed permanent.

Prefer secure direct provisioning. Physical approval alone does not encrypt HTTP LAN transport. Never put refresh tokens/client secrets in Git, a distributed firmware, JavaScript, API responses or logs. One personal device is not authorization to distribute a shared secret across a fleet.

## Data and retries
Calendar: minimum necessary scopes, deterministic event ID and reconciliation after unknown results, ETag before updating. Tasks: no guaranteed idempotency key; reconcile uncertain insert before retrying. No exactly-once claim from blind retries.

Google Keep is not assumed available for personal accounts. Evaluate user-visible Drive files or another accepted notes destination independently; an appData folder alone does not make notes visible on the phone.

## References
- https://developers.google.com/oauthplayground/
- https://developers.google.com/identity/protocols/oauth2/web-server#offline
- https://developers.google.com/identity/protocols/oauth2/limited-input-device
- https://developers.google.com/workspace/calendar/api/auth
- https://developers.google.com/workspace/tasks/reference/rest/v1/tasks

Research: google_feasibility (gpt-6.1-sol / medium), followed by a bounded Playground verification. No credentials collected; no live Google requests tested.
