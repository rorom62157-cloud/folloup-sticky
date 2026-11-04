# Status — 2026-11-04 — first foundation milestone

## État courant de reprise — session Work du 4 novembre

### Publication et passage à un autre ordinateur

Romain demande explicitement le 4 novembre de publier toutes les mises à jour de code et documentation vers `rorom62157-cloud/folloup-sticky`, branche `work/mon-compagnon-v1`, sous `mon-compagnon/`, pour reprise sur un autre ordinateur. Cette demande apporte l'autorisation de publication qui manquait au checkpoint historique. Le head distant contrôlé avant publication est `8c7e28c79a92924299eac13a743ba2d27474d2fc`. Publication par arbre/commit descendants de ce head, sans remplacement de l'amont ni modification de main.

Guide de reprise : `docs/handover.md`. Les deux tentatives Computer Use échouent avec `Sky Computer Use service startup request failed`, puis l'outil disparaît de la session. Aucune action Arduino IDE effectuée. Sur le nouvel ordinateur, revérifier les capacités et le port, sans supposer le même blocage. Installation de `p` toujours à confirmer, aucune nouvelle preuve matérielle.

Cette section prévaut sur les constats historiques ci-dessous. Lecture préalable de AGENTS.md, specification, contracts, status et hardware-test-status effectuée. Branche locale `work/mon-compagnon-v1`, point initial `abe395f` ; checkpoint local avant préparation de publication `9f10613`.

Commit d'implémentation vérifié : `460828d` (`feat: add FAT note adapter and guard diagnostic updates`). Les résultats de ce lot s'appliquent à ses sources ; le présent complément ne modifie que la documentation. Bootloader et application inspectés hors ligne : cible S3/16MB/DIO80, checksum et hash internes valides ; application binaire 340576 octets.

- Deux nouvelles tentatives d'ouverture série directe de `/dev/cu.usbmodem14501`, dont une après « reteste » de Romain : refus `EPERM / Operation not permitted` avant toute commande `i`/`p`. Aucun reset, flash ou effacement effectué par cette session. Pas de contournement. L'installation de `p` reste inconnue ; les résultats matériels transmis par Romain (S3 rev2, flash16Mo, PSRAM8Mo, boutons) restent acquis.
- Adaptateur réel `firmware/storage/posix_note_files.*` pour NoteStore, sur répertoire privé FAT déjà monté et préparé par le futur propriétaire applicatif. Ne monte, ne crée et ne formate pas la SD. Tests sur fichiers réels : persistance/reprise, bornes, liens symboliques, répertoire indisponible, écriture exclusive, renommage sans remplacement, échec réel d'écriture. Tests stricts et ASan/UBSan réussis ; revue indépendante sans défaut confirmé. Compatibilité compilée ESP32-S3 : 325418 octets programme / 22628 RAM statique. Le probe reste COMPILE-ONLY, jamais une cible de flash.
- Lanceur corrigé après revue : mise à jour sans effacement complet par défaut et via `--update-only` ; effacement intégral uniquement avec option explicite `--erase-and-install`. Port attendu puis MAC provenant du numéro USB documenté vérifiés ; si la MAC eFuse diffère, arrêt et réidentification avant toute écriture. Secure Boot et chiffrement doivent chacun être explicitement désactivés. Compilation en répertoire neuf, schéma `default` épinglé, validation binaire des six partitions et de leur MD5 avant écriture. `--update-only` remplace toujours les secteurs bootloader/table/OTA/app : ce n'est pas une lecture seule.
- Quatre régressions Python passent, dont exécution du vrai lanceur avec faux esptool dans un environnement temporaire : défaut sans erase, option explicite, refus sécurité activée avant write, format MAC réel, table modifiée malgré MD5 valide. Aucun matériel sollicité. Les quatre suites existantes core/WAV/notes/HTTP passent.
- Diagnostic recompilé : 340426 octets programme / 24012 RAM statique ; table produite conforme. Aucune nouvelle validation matérielle écran/audio/SD/capteurs/veille.

Prochaine étape matérielle : depuis un environnement autorisé à ouvrir le port, envoyer `i` puis `p` au diagnostic actif ; conserver les six réponses. Si `p` n'est pas disponible, utiliser `scripts/Preparer_module.command --update-only` après fermeture du moniteur, puis retester. Les barrières du lanceur n'ont été exercées qu'avec un faux outil et des images locales, pas sur la carte.

Prochaine étape développement : intégrer montage SD sans formatage et répertoire privé avec un propriétaire applicatif sérialisé ; brancher cet adaptateur aux routes notes et à l'UI. Valider fsync/FatFS et présence/retrait de carte sur matériel avant de déclarer la sauvegarde SD fonctionnelle. Écran/codec/PMIC restent à intégrer à une vraie cible applicative ; ne pas flasher les probes.

## Authorization and source
User approved development. Remote base: folloup-waveshare, SHA 1682b7e7d7ff9c6b23ab7e903c44b241f095b402. Delivery branch work/mon-compagnon-v1, new directory mon-compagnon/. Upstream firmware and main unchanged. Local history is independent; publish through GitHub API on remote ancestry, never force-push local root.

## Delivered in this milestone
- Portable C++17 sleep, recording/classification and physical session authorization policies; strict host tests pass.
- Streaming WAV writer with four-minute cap, FAT-compatible publish and fault-injection tests.
- French vanilla companion web client; contract and no-backend limitations explicit.
- Hardware pin reference, source branch and PWR/BOOT constraints.
- Arduino diagnostic and ESP32-S3 core/WAV smoke build compiled and linked successfully, not flashed.
- Specification, contracts, architecture, decisions, checks and agent worklog for IDE continuation.

## Review
Core independent review found pending-reopen and screen-change signaling defects; fixed with regressions and passing integration tests. Web review found an out-of-order refresh race; latest-request and session-generation guards implemented. Three Node transport regression scenarios pass. Full browser/device validation still pending.

## Environment and blockers
macOS with clang++, Python3.9 and Node through runtime. Arduino CLI1.5.1 / Espressif3.3.8 installed. ESP-IDF standalone tools not found. Shell outbound GitHub DNS unavailable; gh auth invalid; GitHub connector works.
USB /dev/cu.usbmodem14501 detected and read-only access succeeds. esptool opening the port is denied (Operation not permitted). No flash/erase succeeded and no firmware-on-device acceptance claimed.

## Google route
Manual OAuth Playground with user-owned Web client, Offline refresh token is the proposed developer provisioning route without personal backend. See ADR002. Previous statement that a native companion was mandatory was corrected. No credentials collected and no real Google calls tested.

## Next inputs and work
1. Integrate hardware adapters and application UI using verified Waveshare source, with license attribution for any reuse. Resolve reproducible production framework/toolchain (ESP-IDF preferred; Arduino available diagnostic only).
2. Integrate the WAV writer with microphone/SD drivers; implement persistent note/index/outbox and partial recovery; couple domain decisions to real operations.
3. Implement embedded HTTP backend enforcing physical session auth/CSRF on every private route, then browser integration tests.
4. Add Gemini and Google transports using real provisioned credentials only when needed; never fake ready/sync state.
5. Validate physical buttons/display/audio/SD and both sleeps with verified firmware once flash access works. Preserve recovery path.
6. PDF/EPUB, weather/capteurs and orientation remain planned. Tiny LLM deferred.

The assistant application is not complete or runnable on the device yet. Continue existing authorization; do not request a new go for routine next implementation. Ask only for concrete missing credentials or hardware access when that step is ready.

## Second integration slice — 2026-11-04

Previous published head: `8c7e28c79a92924299eac13a743ba2d27474d2fc`; local foundation: `00846b8`. Subsequent changes preserve that remote ancestry and remain under `mon-compagnon/`.

- Added NoteStore for create/edit/delete/pin, optimistic versions, bounded UTF-8 and two checksummed snapshots through an injected filesystem. Strict host and sanitizer tests pass. Any existing corrupt committed snapshot now blocks mutations and preserves files: independent review reproduced stale-ID deletion after silent generation rollback, fixed with a regression. Real SD adapter still required.
- Added host-tested HTTP boundary: exact Host/Origin checks, CSRF, cookie binding, request-size/path bounds, private-by-default data routes and polling that cannot renew sessions.
- Added actual ESP-IDF `esp_http_server` transport and embedded client assets, compiled through installed Arduino 3.3.8. Session bootstrap/status/revoke implemented; physical-decision methods exist but are not wired to buttons. Network receive/send occurs outside the session mutex after review. Unsupported application routes return 501; no invented state or successful settings writes.
- Transport harness compiled: 463654 program bytes, 25068 global RAM bytes (final transport build). It is compile-only, NEVER a flash target; no Wi-Fi, UI or SD initialization.
- New hardware attempt again denied opening USB with Operation not permitted. Diagnostic rebuilt and inspected, no erase/write/SD access. See `hardware-test-status.md` for verified-backup and limited-flash/recovery procedure. Recovery must run in an environment authorized to open serial, without bypassing this session restriction.

Next ready work: production SD filesystem adapter and note routes; shared application owner/task for SD/audio/UI; establish a runnable hardware integration target with screen/codec/PMIC drivers. Keep unsupported services explicit. Continue authorized development; do not ask for a new go. Hardware acceptance remains blocked on serial access.

Full portable core including new notes and HTTP sources cross-compiled/linked: 319014 program bytes, 22628 global RAM bytes. This compile-only smoke does not exercise those features on-device.

Client corrections completed: stale initial session checks cannot cancel a new access request; note limits align with storage and encoded HTTP limits. Expanded Node transport/editor regression suite passed on final source.

## Publication checkpoint

Local implementation commit: `ccd849f` (23 files). Tests and final compile results above apply to that implementation. Remote publication via create_tree was rejected by automatic approval review before any tree/commit/ref update succeeded. Stated reason: potentially sensitive personal/hardware metadata and insufficient trusted authorization for destination/payload. Do not route around the rejection. Existing remote head remains the previously retrieved `8c7e28c79a92924299eac13a743ba2d27474d2fc`; the current lot is local until the user explicitly approves publication to `rorom62157-cloud/folloup-sticky`, branch `work/mon-compagnon-v1`, path `mon-compagnon/`. No main update or merge is requested.

## Dernière demande de préparation matérielle

Utilisateur : effacement interne sans sauvegarde autorisé, SD également effaçable si utile. Accès série encore refusé lors de la dernière tentative ; aucun flash/effacement réalisé. Lanceur local `scripts/Preparer_module.command` préparé pour exécution manuelle dans un Terminal autorisé. Il efface la flash et installe/vérifie le diagnostic après compilation et contrôles ; il ne touche pas à la SD. Syntaxe vérifiée seulement ; tests matériels en attente du succès réel du lanceur. Ne pas automatiser son ouverture dans un autre outil pour contourner le refus d’accès.

## Diagnostic exécuté sur la carte — résultat fourni par Romain

Le journal série envoyé par l’utilisateur confirme l’exécution de notre diagnostic sur ESP32-S3 révision 2, deux cœurs à 240 MHz, SDK v5.5.4. Flash détectée : 16777216 octets ; PSRAM : 8388608 octets ; heap libre : 298100 ; PSRAM libre : 8386096. Ce résultat confirme le démarrage du diagnostic et la reconnaissance des mémoires. Il ne prouve pas à lui seul l’effacement de tous les secteurs, le succès d’une vérification flash ni les autres périphériques.

Prochaine recette physique : appui/relâchement rocker haut, clic, bas et BOOT, avec événements PRESSED/RELEASED dans le moniteur série. PWR géré par PMIC et non testé ici ; écran/audio/SD, capteurs et veilles toujours non validés. Accès série direct de l’agent toujours bloqué ; observation matérielle transmise par l’utilisateur.

## Boutons physiques — journal fourni par Romain

Appui et relâchement confirmés sur ROCKER_UP GPIO4 (102553/102937 ms), ROCKER_CLICK GPIO5 (104881/105223 ms), ROCKER_DOWN GPIO6 (106217/106443 ms) et BOOT GPIO0 (113645/113863 ms). Deux cycles BOOT supplémentaires également présents. Les heartbeats continuent jusqu’à 120001 ms : le diagnostic reste actif pendant ce scénario. Ce test valide les entrées directes et leurs événements débouncés ; il ne valide pas les appuis longs fonctionnels, l’enregistrement, le réveil profond ni PWR/PMIC.

Prochaine preuve attendue : lignes I2C 0x34/0x18/0x51/0x6A/0x6B/0x70 du début de journal de démarrage ; présence adresse seulement, pas validation fonctionnelle des composants.

## Diagnostic interactif I²C

Le début du journal n’étant plus visible côté utilisateur, ajout de la commande série `p` pour relancer les sondes adresse seule, sans redémarrage. Le lanceur accepte `--update-only` pour éviter un nouvel effacement intégral pendant cette mise à jour. Compilation ESP32-S3 réussie : 340410 octets programme, 24012 octets statiques. Syntaxe shell/diff vérifiés. Mise à jour matérielle non effectuée par l’agent, toujours sans accès au port ; installation et retour `p` attendus de l’utilisateur. Les images reconstruites ont de nouvelles empreintes : ne pas utiliser les anciennes sommes de la documentation comme références de cette version.
