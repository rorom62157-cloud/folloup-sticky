# Mon compagnon — delivery rules

Read docs/specification.md, docs/contracts.md and docs/status.md first. This is a new application, not a modification of the reference firmware. Reference: rorom62157-cloud/folloup-sticky branch folloup-waveshare at 1682b7e7d7ff9c6b23ab7e903c44b241f095b402. Preserve upstream files.

No Astra subagents. Default gpt-6.1-sol/medium for implementation, high for bounded critical reviews; gpt-6-luna/medium for documentation. Announce model/thinking. Every assignment has inputs, exclusive file ownership, deliverable, checks and termination. No waiting loops. Parallelize only independent ready work; review then correction then integration.

User approved development and necessary verification. Host tests for this independent application are authorized. Do not build the upstream reference automatically. No flash without verified target image/partition and recovery plan. Do not invent test success or device capability.

Keep core pure C++17, adapters own ESP-IDF. No secrets in Git/logs/web responses. Web data routes require session authorization. Romain explicitly suspended physical invitations for the current local release: PairingMode::LocalPassword on the protected AP is authorized; retain origin/CSRF/session checks. Caller serializes core access. Preserve SD originals before AI. Never report success for a stub. Google OAuth local-only feasibility is unresolved; no silent backend dependency.

Use meaningful commits and feature branch. Local directory is an independent working repository; remote publishing uses GitHub API into mon-compagnon/ preserving upstream base tree because shell network is unavailable. Do not push the local root commit over upstream history. Document commit identities and checks in docs/status.md. Workspace edits are user-owned; no destructive resets.
