# ADR 001 — Independent domain, reference retained

Accepted implementation choice after user go, 2026-11-04.

Keep upstream firmware unchanged. Add our application under mon-compagnon/ on work/mon-compagnon-v1, based on folloup-waveshare. Pure C++17 domain owns product policies. Hardware adapters, network transport and display have separate responsibilities. Reference is studied for known-good mapping and patterns; any copied implementation needs license attribution. No reference source copied into the initial domain.

Host clang++ is available. ESP-IDF tools are not installed on PATH. Arduino ESP32 3.3.8 and arduino-cli 1.5.1 are installed; use them for an isolated hardware diagnostic, not a claim that the production architecture has been completed. Production framework decision remains ESP-IDF preferred until reproducible toolchain is available or Arduino integration is deliberately accepted.

Shell GitHub DNS fails and gh token is invalid; use authenticated GitHub connector to publish changes on existing remote history. Local independent repository has different root history: NEVER force-push it over upstream. Publication builds a remote Git tree using upstream base tree and adds only mon-compagnon/ paths, then creates a commit and advances the feature branch with a head check.
