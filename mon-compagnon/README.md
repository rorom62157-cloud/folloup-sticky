# Mon compagnon

Assistant personnel sur Waveshare ESP32-S3-ePaper-3.97, écran non tactile 800 × 480. Projet indépendant développé à partir des besoins de Romain ; FolloUp Waveshare sert de référence matérielle et ergonomique.

**État : version0.2 installable sur appareil, orientée usages locaux en portrait.** Accueil et applications Notes/Post-it/Tâches/Fichiers texte, réglages et accès web. Voir [le guide0.2](docs/device-0.2.md) et les observations réelles dans docs/status.md. Audio, services externes et veilles restent à intégrer.

## Contenu

- `core/` : logique C++17 testable de veille, capture/classification et autorisation physique du compagnon, écriture WAV par blocs, stockage versionné des notes/post-it et contrôle des requêtes HTTP.
- `web/` : compagnon web français, sans dépendance distante, branché sur un contrat API explicite. Un backend approuvé est nécessaire ; pas de faux succès lorsque absent.
- `firmware/companion_probe/` : transport HTTP ESP-IDF, sessions et ressources web ; harness de compilation uniquement, sans connexion Wi-Fi ni pilotes applicatifs.
- `firmware/diagnostic/` : identification matérielle et entrées, isolée du firmware utilisateur.
- `firmware/storage/` : adaptateur de notes pour un répertoire privé FAT déjà monté, avec tests sur fichiers réels ; probe de compilation séparé.
- `docs/specification.md` : cahier des charges approuvé.
- `docs/contracts.md` : interfaces de ce lot.
- `docs/hardware.md` : mapping sourcé et contraintes BOOT/PWR.
- `docs/status.md` : état réel, vérifications et prochaine étape.
- `AGENTS.md` : reprise dans l'IDE et politique agentique sans Astra.

## Vérification locale

```sh
./scripts/test-core.sh
./scripts/test-storage.sh
sh scripts/test-notes.sh
sh scripts/test-http.sh
sh scripts/test-note-service.sh
sh scripts/test-note-json.sh
sh scripts/test-note-http.sh
sh scripts/test-note-files.sh
python3 tests/diagnostic_preflight_tests.py
sh scripts/build-http-probe.sh
./scripts/build-core-smoke.sh
./scripts/build-diagnostic.sh
sh scripts/build-storage-probe.sh
node tests/web_races.mjs
```

Les suites C++ utilisent clang++/C++17. Les probes compilent et lient les modules avec la cible ESP32-S3 ; leurs images ne doivent pas être flashées. Le diagnostic utilise Arduino CLI et le core Espressif installé, cible ESP32-S3 16MB/PSRAM octale/USB CDC. Ce diagnostic ne remplace pas une compilation du firmware final ESP-IDF. Les commandes de vérification ci-dessus ne téléversent rien ; seul le lanceur de préparation explicitement exécuté écrit sur la carte.

## Reprendre le travail

Pour un autre ordinateur, commencer par [docs/handover.md](docs/handover.md), puis lire `AGENTS.md`, `docs/status.md` et le contrat du module concerné. Les missions doivent avoir un input, un output et une fin. Une revue indépendante vérifie les défauts concrets avant intégration. Les intégrations non validées, notamment OAuth Google sans serveur personnel, ne doivent jamais être remplacées par des simulacres.

## Historique et branche

Référence : https://github.com/rorom62157-cloud/folloup-sticky/tree/folloup-waveshare au commit `1682b7e7d7ff9c6b23ab7e903c44b241f095b402`.

Livraison : branche `work/mon-compagnon-v1`, dossier `mon-compagnon/`, sans modification de l'application amont. Le dossier local de travail a un historique indépendant car le terminal ne peut pas cloner GitHub ; les publications passent par l'API en conservant l'historique distant. Ne jamais forcer un push de l'historique local vers le dépôt de référence.

Aucune clé API, aucun token, aucun audio personnel ne doit être ajouté au dépôt.


Les routes de notes peuvent désormais être raccordées à un NoteStore persistant
par `NoteHttp` ; le web accepte un mode notes seul. Voir l'état courant de
`docs/status.md` pour les conditions d'intégration. Aucun probe n'est une image
applicative à flasher.


## Première alpha appareil

`sh scripts/build-device.sh` construit maintenant la vraie cible notes locale
(écran, boutons, SD, web sous accord physique). Guide et limites :
[docs/device-alpha.md](docs/device-alpha.md). Ce n'est pas encore la V1 complète.
Le statut d'installation et les observations réelles sont dans docs/status.md.
