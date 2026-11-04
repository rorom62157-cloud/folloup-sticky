# Reprise sur un autre ordinateur — 4 novembre 2026

## Récupérer le travail

Dépôt : `https://github.com/rorom62157-cloud/folloup-sticky`.
Branche : `work/mon-compagnon-v1`. Projet : sous-dossier `mon-compagnon/`.

```sh
git clone --branch work/mon-compagnon-v1 --single-branch https://github.com/rorom62157-cloud/folloup-sticky.git
cd folloup-sticky/mon-compagnon
git status --short
```

Lire dans cet ordre : `AGENTS.md`, ce document, `docs/status.md`,
`docs/hardware-test-status.md`, `docs/specification.md` et `docs/contracts.md`.
La section « État courant » prévaut sur les anciens comptes rendus chronologiques.

L'historique local d'origine est indépendant de celui du dépôt distant. La
publication crée un commit descendant du précédent head distant `8c7e28c`,
sans réécrire les commits amont. Les SHA locaux cités dans les documents servent
de références de travail ; ils ne sont pas nécessairement présents dans le clone.

## État effectivement acquis

- Carte : diagnostic exécuté selon les journaux fournis par Romain ; ESP32-S3
  révision 2, flash 16 Mo, PSRAM 8 Mo. Haut/clic/bas et BOOT : événements
  d'appui et relâchement validés. PWR/PMIC non validé.
- Commande `p` développée et compilée pour répéter les sondes I²C ; installation
  de cette version sur la carte encore inconnue. `i` répète l'identité.
- Noyau portable C++17, WAV progressif, notes versionnées, frontière HTTP,
  transport HTTP ESP-IDF et client web présents, avec tests et revues documentés.
- Adaptateur `PosixNoteFiles` ajouté et compilé : requiert une SD FAT déjà montée,
  un répertoire privé précréé et un propriétaire applicatif sérialisé. Aucun
  montage/formatage automatique. Tests sur vrais fichiers du Mac réussis.
- Aucun firmware applicatif complet : écran, audio, montage SD, UI physique,
  réseau et intégration des services restent à assembler. Les routes non
  implémentées répondent explicitement 501. Pas de connexion Google/Gemini réelle.

Les refus série et la panne de Computer Use concernent l'ancienne session :
ne pas les considérer comme la preuve d'un blocage sur le nouvel ordinateur.
Computer Use a échoué au démarrage puis est devenu indisponible ; aucune action
Arduino IDE n'a été réalisée par ce moyen. Aucune tentative ne doit contourner
un refus de sécurité du nouvel environnement.

## Première action sur la carte

1. Identifier le port actuel et la même carte ; l'ancien Mac utilisait
   `/dev/cu.usbmodem14501`, numéro USB `44:B1:76:B6:A5:74`. Les ports et chemins
   d'outils peuvent changer. Ne pas téléverser sur un autre appareil par défaut.
2. Sans flash/reset, ouvrir le moniteur à 115200 et envoyer `i`, puis `p`.
   Archiver les réponses aux adresses 0x34/0x18/0x51/0x6A/0x6B/0x70.
   Un ACK indique seulement une présence, pas un test fonctionnel.
3. Si `p` ne répond pas, fermer le moniteur et mettre à jour le diagnostic
   uniquement après validation des images/partitions/identité/sécurité et de la
   récupération USB. Utiliser `scripts/Preparer_module.command --update-only`
   sur Mac compatible, puis rouvrir le moniteur et retester `i`/`p`.
4. Le lanceur est volontairement lié à l'ancien port et à la MAC enregistrée.
   Si le port change, adapter le contrôle de port après identification de la
   carte ; conserver le contrôle d'identité. Une divergence de MAC USB/eFuse
   nécessite une investigation, pas la suppression de la barrière.

Le lanceur sans argument fait aussi une mise à jour limitée ; il écrit tout
de même bootloader/table/OTA/app et leurs secteurs nécessaires. Ne pas utiliser
`--erase-and-install` pour cette reprise. Ne pas formater la SD.
Les images `core_smoke`, `companion_probe` et `storage_probe` sont uniquement des
preuves de compilation : **ne jamais les flasher**. L'écran peut garder une
ancienne image avec le diagnostic, qui ne l'initialise pas.

## Outils et contrôles reproductibles

Dernier environnement validé : Arduino CLI 1.5.1, Arduino-ESP32 3.3.8,
ESP-IDF SDK 5.5.4 fourni par ce core, esptool 5.2.0, Python 3.9 et clang++ C++17.
Pas d'ESP-IDF standalone validé. Ne pas compiler automatiquement le projet amont.
Les scripts ciblent Mac ; installer les mêmes versions et ajuster les chemins
au nouvel ordinateur. Les builds acceptent `ARDUINO_CLI` ; le lanceur Mac utilise
les outils sous `$HOME/Library/Arduino15` et doit être inspecté sur un autre OS.

```sh
sh scripts/test-core.sh
sh scripts/test-storage.sh
sh scripts/test-notes.sh
sh scripts/test-http.sh
sh scripts/test-note-files.sh
python3 tests/diagnostic_preflight_tests.py
node tests/web_races.mjs

# Compilation seulement, aucun téléversement :
sh scripts/build-diagnostic.sh
sh scripts/build-core-smoke.sh
sh scripts/build-http-probe.sh
sh scripts/build-storage-probe.sh
```

Résultats déjà obtenus : suites core/WAV/notes/HTTP et vrais fichiers passantes ;
nouvel adaptateur ASan/UBSan passant ; quatre régressions du lanceur passantes.
Derniers builds : diagnostic 340426 octets programme/24012 statiques ; adaptateur
325418/22628. Les images diagnostic ont checksum/hash internes valides ; leurs
empreintes peuvent changer à la recompilation. Les tests web précédents sont
documentés ; ils n'ont pas été relancés dans le dernier lot sans changement web.

## Suite du développement

Après les sondes : intégrer les pilotes écran/PMIC/codec depuis les sources
épinglées avec attribution ; créer une vraie cible applicative. Intégrer le
montage SD sans formatage et le stockage des notes/WAV dans une tâche propriétaire,
puis les routes notes et l'autorisation physique. Tester sur carte avant de
déclarer chaque fonction opérationnelle. Google OAuth suit la piste manuelle de
`docs/decisions/002-google-authorization.md` ; aucun secret n'a été provisionné.

Conserver la méthode agentique : aucun Astra ; implémentation gpt-6.1-sol/medium,
revue sensible high, documentation luna/medium si disponible. Missions bornées,
propriétaires disjoints, sortie avec tests/limites, arrêt après livraison.
Revue puis correction puis intégration ; aucun agent en attente active.

Romain a explicitement demandé la publication de tout le code et des documents
sur la branche ci-dessus le 4 novembre. Les précédents refus de publication
restent un historique ; ne pas demander un nouveau go pour continuer le travail
autorisé. Ne pas publier de secrets ou d'audios, ne pas modifier main et ne pas
forcer l'historique local sur le distant.
