# Changelog

## 0.3.0 storage and hardware handoff — 2026-11-04

- Concrete FAT/POSIX note adapter, real filesystem tests and compile-only ESP32 storage probe.
- Interactive I²C diagnostic command `p`, compiled; installation still unconfirmed.
- Diagnostic launcher defaults to update-only, checks identity/security/partition table and uses a fresh build directory.
- User-provided boot/memory/button observations recorded separately from unvalidated peripherals.
- Cross-computer handover with source retrieval, toolchain, safe first serial test and remaining implementation work.
- Serial access remained denied; Computer Use failed to start and later became unavailable. No device flash by this session.

## 0.2.0 integration foundations — 2026-11-04

- Versioned notes/post-it store with injected filesystem and interruption/corruption tests.
- Cookie/CSRF/Origin HTTP boundary and ESP-IDF server transport compiling for ESP32-S3.
- Physical session mutex kept outside all network I/O; corruption blocks stale-ID reuse.
- Web bootstrap/UTF-8 bounds corrections; diagnostic USB mode and recovery procedure.
- Still not a runnable assistant firmware: SD/UI/audio/network adapters remain to integrate; hardware port access denied.

## 0.1.0 foundation — 2026-11-04

- New independent portable domain for sleep, capture classification and physical companion approval.
- Streaming PCM16LE WAV writer with bounded memory, recoverable partials and FAT rename regression tests.
- French companion frontend implementing explicit API contracts; backend integration pending.
- Hardware reference and bounded diagnostic compiling on installed ESP32 toolchain.
- Agent workflow, source references, OAuth provisioning study and IDE handoff documents.
- Independent review corrections: reopen pending recordings and signal screen-only sleep changes.

Not a release of the complete assistant firmware; no on-device application validation yet.


## Reprise logicielle — notes HTTP persistantes

Service notes, JSON strict, dispatch CRUD versionné et raccordement transport
ESP-IDF avec backend explicite. Mode notes seul dans le web quand l'état complet
est indisponible. Tests de persistance sur fichiers réels, autorisation, Unicode,
conflits et courses de repli web ; compilation ESP32-S3 réussie. Aucun flash.


## Alpha notes appareil

Cible applicative distincte des probes : écran SSD1677 GPLv3 attribué, rendu
monochrome borné, clavier/navigation physiques, montage SD sans formatage,
notes/post-it et serveur local à autorisation physique. Protection contre accord
avant affichage, demandes répétées et affichage trompeur de clé Wi-Fi corrigés
après revue. Compilation et tests hôte passants. Après reconnexion, sauvegarde vérifiée des
secteurs remplacés, installation/vérification réussie, démarrage et test SD
réel passants ; menu confirmé visible par Romain.


## Version0.2 — portrait et applications locales

Accueil et vrai menu Applications, notes avec titres et actions, post-it dédiés,
tâches persistantes et cochables, lecteur TXT/MD en lecture seule, réglages
mémorisés et aide. Rendu portrait dans les deux sens, écran dans une tâche à
double buffer, confirmations après affichage effectif, rafraîchissement rapide
sourcé et nettoyage périodique. Panne réseau sans blocage du local ; panne écran
révoque l'accès web. Tests hôte et premier démarrage/stockage notes+tâches sur
carte passants ; sens du portrait ajusté au retour visuel de Romain.
