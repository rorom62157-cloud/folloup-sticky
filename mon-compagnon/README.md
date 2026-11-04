# Mon compagnon

Assistant personnel sur Waveshare ESP32-S3-ePaper-3.97, écran non tactile 800 × 480. Projet indépendant développé à partir des besoins de Romain ; FolloUp Waveshare sert de référence matérielle et ergonomique.

**État : premier lot en développement, pas encore un firmware utilisateur complet.** Ne pas flasher un diagnostic en pensant installer l'application. Les pilotes, l'affichage et les connecteurs ne sont pas validés sur l'appareil.

## Contenu

- `core/` : logique C++17 testable de veille, capture/classification et autorisation physique du compagnon, et écriture WAV par blocs avec conservation des fichiers partiels.
- `web/` : compagnon web français, sans dépendance distante, branché sur un contrat API explicite. Un backend approuvé est nécessaire ; pas de faux succès lorsque absent.
- `firmware/diagnostic/` : identification matérielle et entrées, isolée du firmware utilisateur.
- `docs/specification.md` : cahier des charges approuvé.
- `docs/contracts.md` : interfaces de ce lot.
- `docs/hardware.md` : mapping sourcé et contraintes BOOT/PWR.
- `docs/status.md` : état réel, vérifications et prochaine étape.
- `AGENTS.md` : reprise dans l'IDE et politique agentique sans Astra.

## Vérification locale

```sh
./scripts/test-core.sh
./scripts/test-storage.sh
./scripts/build-core-smoke.sh
./scripts/build-diagnostic.sh
node tests/web_races.mjs
```

Les deux suites de tests utilisent clang++/C++17. Le smoke test compile et lie le noyau et le stockage avec la cible ESP32-S3 ; son image ne doit pas être flashée. Le diagnostic utilise Arduino CLI et le core Espressif installé, cible ESP32-S3 16MB/PSRAM octale/USB CDC. Ce diagnostic ne remplace pas une compilation du firmware final ESP-IDF. Aucun script ne flashe automatiquement.

## Reprendre le travail

Lire `AGENTS.md`, `docs/status.md`, puis le contrat du module concerné. Les missions doivent avoir un input, un output et une fin. Une revue indépendante vérifie les défauts concrets avant intégration. Les choix non résolus, notamment OAuth Google sans serveur personnel, ne doivent jamais être remplacés par des simulacres.

## Historique et branche

Référence : https://github.com/rorom62157-cloud/folloup-sticky/tree/folloup-waveshare au commit `1682b7e7d7ff9c6b23ab7e903c44b241f095b402`.

Livraison : branche `work/mon-compagnon-v1`, dossier `mon-compagnon/`, sans modification de l'application amont. Le dossier local de travail a un historique indépendant car le terminal ne peut pas cloner GitHub ; les publications passent par l'API en conservant l'historique distant. Ne jamais forcer un push de l'historique local vers le dépôt de référence.

Aucune clé API, aucun token, aucun audio personnel ne doit être ajouté au dépôt.
