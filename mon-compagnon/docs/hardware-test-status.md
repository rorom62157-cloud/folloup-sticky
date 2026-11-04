# Diagnostic matériel — 4 novembre 2026

## Dernier contrôle — reprise Work

Deux ouvertures directes sans reset de `/dev/cu.usbmodem14501`, dont une retentative demandée par Romain, échouent encore avec `[Errno 1] Operation not permitted`. Les commandes `i` et `p` n'ont donc pas été envoyées. Aucun flash/effacement, aucun accès SD. Les observations transmises par Romain plus bas restent valides ; seule l'installation de la version interactive `p` reste à confirmer.

Le lanceur a été durci et relu indépendamment. Sans argument il fait désormais une mise à jour sans effacement intégral ; `--update-only` est équivalent. L'ancien comportement destructif exige `--erase-and-install` explicite. Il refuse un autre port que celui demandé et une MAC différente du numéro USB enregistré ; une divergence USB/eFuse exige une réidentification, sans désactiver la barrière. Les deux lignes de sécurité doivent confirmer Secure Boot et Flash Encryption désactivés. Le schéma de partitions est épinglé et sa table binaire/MD5 contrôlée ; un dossier de compilation neuf évite un ancien `partitions.csv` conservé par Arduino.

Tests hors matériel : quatre régressions Python réussies (`python3 tests/diagnostic_preflight_tests.py`), syntaxe shell et revue indépendante réussies. Le test du lanceur utilise uniquement un faux outil et des fichiers temporaires. Diagnostic recompilé : 340426 octets programme, 24012 RAM statique ; validation de la table générée réussie. Ces résultats n'attestent pas d'une installation.

Inspection hors ligne finale des deux images avec esptool 5.2.0 `image-info` : bootloader et application ciblent ESP32-S3 (ID9), 16MB DIO80MHz ; checksum et hash internes valides. Application binaire 340576 octets, sous app0. Aucun port série sollicité par cette inspection.

Les commandes et empreintes historiques ci-dessous décrivent les anciennes constructions ; ne pas les utiliser comme sommes de la nouvelle image. `--update-only` écrit les quatre images séparées et efface leurs secteurs nécessaires, jamais toute la flash ni la SD. Récupération : diagnostic recompilable et ROM USB, sous réserve d'accès série effectif et des contrôles du lanceur.

## Résultat réel

La préparation du MCU pour essais est autorisée, en préservant la microSD. Aucune commande d'effacement ou d'écriture flash n'a été exécutée. Aucun accès à la microSD n'a été tenté.

- macOS expose `/dev/cu.usbmodem14501` et `/dev/tty.usbmodem14501`.
- `system_profiler SPUSBDataType` identifie « USB JTAG/serial debug unit », Espressif VID `303a`, PID `1001`, numéro `44:B1:76:B6:A5:74`, emplacement `0x14500000`. Cela identifie l'interface USB, pas la révision exacte du produit ni sa capacité flash.
- Arduino CLI `1.5.1`, core `esp32:esp32` `3.3.8`, esptool `5.2.0` installés. Aucun téléchargement ajouté.
- `esptool --chip esp32s3 --port /dev/cu.usbmodem14501 chip-id` échoue, code 2 : `[Errno 1] Operation not permitted`. Le nœud existe avec mode `crw-rw-rw-` ; ses permissions Unix visibles ne suffisent pas à autoriser son ouverture depuis cette session.
- Aucune identité MCU, sécurité eFuse, partition actuelle ou sauvegarde flash n'a pu être lue. Aucun contournement de la restriction ni élévation de privilèges n'a été tenté.

Le blocage précède toute sauvegarde et empêche donc le flash réversible demandé. L'autorisation utilisateur n'est pas en cause ; il faut un environnement qui puisse ouvrir ce port série.

## Image de diagnostic

Source indépendante : `firmware/diagnostic/diagnostic.ino`. Aucun écran, audio, réseau, montage SD ou réglage PMIC ; boutons GPIO0/4/5/6 et sondes I2C adresse seule. Le script `scripts/build-diagnostic.sh` utilise désormais explicitement `USBMode=hwcdc`, cohérent avec le README.

Compilation réellement exécutée avec Arduino CLI 1.5.1 / core 3.3.8 : succès, code 0 ; 340346 octets de programme sur 1310720 et 24012 octets de variables globales sur 327680. FQBN : `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi`. Fichiers temporaires dans `/tmp/mon-compagnon-hardware-output` et métadonnées dans `/tmp/mon-compagnon-hardware-build`. Le binaire applicatif fait 340496 octets, sous la limite app0. SHA-256 de cette construction :

| Image | SHA-256 |
|---|---|
| application | `15727a61aea05fec07e8efd41df249e60e955c0336d58a127e251185b3d5b524` |
| bootloader | `b68c1ed33c43a4290375213f24d608c125e31e22880d16d4a5dcba89e083716d` |
| partitions | `148b959cbff1c38aa8e1d5c0ba9d612c54997b945e56a63f41223eef650653a1` |
| fusionnée 16 MiB | `2a28654f9626454d8e811f885a7accac21a5f3c0340e95183475456d676963c0` |

Les sommes peuvent changer après recompilation (métadonnées de construction). `sh -n scripts/build-diagnostic.sh` et `git diff --check` réussissent. La compilation ne valide aucun périphérique réel.

`esptool image-info` a validé l'image applicative : puce ID 9 (ESP32-S3), flash 16 MB, DIO 80 MHz, checksum et hash internes valides. Cette inspection est locale, sans accès série ; elle ne confirme pas la puce branchée. Le bootloader n'a pas fait l'objet de cette inspection `image-info` lors de ce passage ; la procédure de reprise demande les deux inspections avant installation.

La cible est ESP32-S3, flash 16 MiB, PSRAM OPI, USB CDC/JTAG matériel. La table Arduino par défaut ne réserve que les premiers 4 MiB : NVS `0x9000/0x5000`, OTA data `0xe000/0x2000`, app0 `0x10000/0x140000`, app1 `0x150000/0x140000`, SPIFFS `0x290000/0x160000`, coredump `0x3f0000/0x10000`. Ce partitionnement suffit au diagnostic mais n'est pas le partitionnement applicatif final. Il ne prouve rien sur la table présente sur le MCU.

La recette Arduino place le bootloader à `0x0`, la table à `0x8000`, `boot_app0.bin` à `0xe000` et l'application à `0x10000`. Le fichier `flash_args` référence `boot_app0.bin` sans le copier dans le dossier de sortie : ne pas l'utiliser aveuglément comme script autonome. L'image fusionnée est créée avec remplissage jusqu'à 16 MiB ; elle écraserait toute la flash si utilisée. Préférer les quatre images séparées pour limiter les secteurs touchés après sauvegarde complète.

## Reprise reproductible depuis un terminal disposant de l'accès USB

Ces commandes sont une procédure à exécuter et contrôler dans l'ordre, pas un compte rendu de succès. Fermer les moniteurs série. Reconfirmer le numéro USB et le port après chaque reconnexion. Si nécessaire, utiliser le mode téléchargement ROM BOOT + reset puis relâcher BOOT ; ne pas maintenir PWR au risque de couper l'alimentation.

```sh
cd '/Users/romainfontaine/Documents/ChatGPT/Projets ESP32/mon-compagnon'
ESPTOOL='/Users/romainfontaine/Library/Arduino15/packages/esp32/tools/esptool_py/5.2.0/esptool'
PORT=/dev/cu.usbmodem14501
CORE='/Users/romainfontaine/Library/Arduino15/packages/esp32/hardware/esp32/3.3.8'
"$ESPTOOL" --chip esp32s3 --port "$PORT" chip-id
"$ESPTOOL" --chip esp32s3 --port "$PORT" flash-id
"$ESPTOOL" --chip esp32s3 --port "$PORT" get-security-info
```

Arrêter si la puce n'est pas ESP32-S3, si la capacité n'est pas 16 MiB ou si chiffrement flash / secure boot sont actifs. Ne pas modifier les eFuses. La restauration brute d'un appareil sécurisé nécessite une procédure spécifique, non validée ici.

Choisir un **nouveau** dossier de sauvegarde privé, hors Git. Après confirmation de 16 MiB et de l'absence des protections ci-dessus :

```sh
BACKUP_DIR="$HOME/MonCompagnonRecovery-20261104"
mkdir -m 700 "$BACKUP_DIR"
"$ESPTOOL" --chip esp32s3 --port "$PORT" read-flash 0 0x1000000 "$BACKUP_DIR/original-16MiB.bin"
test "$(stat -f %z "$BACKUP_DIR/original-16MiB.bin")" = 16777216
shasum -a 256 "$BACKUP_DIR/original-16MiB.bin"
"$ESPTOOL" --chip esp32s3 --port "$PORT" verify-flash 0 "$BACKUP_DIR/original-16MiB.bin"
```

Conserver la sauvegarde et son SHA-256 indépendamment du dépôt : elle peut contenir des secrets du firmware précédent. Si un contrôle échoue, arrêter avant écriture. Lire et archiver aussi la table actuelle avec `read-flash 0x8000 0x1000` vers un fichier distinct ; son interprétation permet de documenter le firmware remplacé. La sauvegarde complète conserve déjà la table, NVS et toutes les partitions.

Compiler et inspecter les images avant flash :

```sh
./scripts/build-diagnostic.sh
"$ESPTOOL" --chip esp32s3 image-info artifacts/diagnostic/diagnostic.ino.bootloader.bin
"$ESPTOOL" --chip esp32s3 image-info artifacts/diagnostic/diagnostic.ino.bin
shasum -a 256 artifacts/diagnostic/diagnostic.ino*.bin
```

Vérifier cible ESP32-S3 et taille de l'application inférieure à `0x140000`. Après réussite de la sauvegarde vérifiée et confirmation des images, installer sans `erase-flash` :

```sh
"$ESPTOOL" --chip esp32s3 --port "$PORT" write-flash --flash-mode keep --flash-freq keep --flash-size keep \
  0x0 artifacts/diagnostic/diagnostic.ino.bootloader.bin \
  0x8000 artifacts/diagnostic/diagnostic.ino.partitions.bin \
  0xe000 "$CORE/tools/partitions/boot_app0.bin" \
  0x10000 artifacts/diagnostic/diagnostic.ino.bin
```

`write-flash` efface uniquement les secteurs nécessaires à ces écritures. Cela remplace tout de même table et application et peut rendre les anciennes données logiques inaccessibles ; la sauvegarde intégrale est indispensable.

Retour au contenu exact de la flash d'origine, sur **le même MCU**, après vérification SHA-256 du fichier sauvegardé et maintien du mode ROM si nécessaire :

```sh
"$ESPTOOL" --chip esp32s3 --port "$PORT" write-flash --flash-mode keep --flash-freq keep --flash-size keep \
  0 "$BACKUP_DIR/original-16MiB.bin"
"$ESPTOOL" --chip esp32s3 --port "$PORT" verify-flash 0 "$BACKUP_DIR/original-16MiB.bin"
```

La sauvegarde flash ne sauvegarde pas la microSD. Cette procédure ne monte, n'efface et ne formate jamais la microSD.

## Acceptation après installation

Ouvrir un moniteur à 115200 bauds ; envoyer `i`. Relever modèle/révision, flash et PSRAM ; vérifier les appuis/relâchements des trois commandes rocker et BOOT. Un ACK I2C ne valide que la présence d'une adresse. L'écran peut conserver son image précédente. PWR, audio, écran, SD, sommeil et autonomie ne sont pas testés par ce diagnostic. Aucun résultat matériel n'est actuellement acquis.

## Autorisation ultérieure et lanceur local

Romain a ensuite explicitement autorisé l’effacement sans sauvegarde, y compris celui de la SD si nécessaire. L’exigence précédente de sauvegarde n’est donc plus une condition demandée par l’utilisateur. La SD n’a pas besoin d’être formatée pour le diagnostic actuel ; elle reste intacte.

Une nouvelle tentative de `chip-id` a de nouveau échoué à l’ouverture avec `Operation not permitted`, sans effacement ni flash. Aucun outil alternatif n’a été utilisé pour contourner ce refus.

`scripts/Preparer_module.command` est destiné à être lancé par l’utilisateur depuis son Terminal Mac : compilation et inspection des images, sélection d’un port unique, contrôle ESP32-S3 et flash 16 MiB, affichage de sécurité, effacement interne sans sauvegarde, installation des quatre images puis vérification. Les erreurs arrêtent la procédure ; aucune option force ni modification eFuse. Aucun accès SD. La syntaxe `bash -n` et `git diff --check` passent ; le lanceur n’a pas été exécuté sur la carte et ne constitue pas une preuve de remise à zéro. Une future exécution doit confirmer le succès matériel avant de lancer les essais.

## Diagnostic exécuté sur la carte — résultat fourni par Romain

Le journal série envoyé par l’utilisateur confirme l’exécution de notre diagnostic sur ESP32-S3 révision 2, deux cœurs à 240 MHz, SDK v5.5.4. Flash détectée : 16777216 octets ; PSRAM : 8388608 octets ; heap libre : 298100 ; PSRAM libre : 8386096. Ce résultat confirme le démarrage du diagnostic et la reconnaissance des mémoires. Il ne prouve pas à lui seul l’effacement de tous les secteurs, le succès d’une vérification flash ni les autres périphériques.

Prochaine recette physique : appui/relâchement rocker haut, clic, bas et BOOT, avec événements PRESSED/RELEASED dans le moniteur série. PWR géré par PMIC et non testé ici ; écran/audio/SD, capteurs et veilles toujours non validés. Accès série direct de l’agent toujours bloqué ; observation matérielle transmise par l’utilisateur.

## Boutons physiques — journal fourni par Romain

Appui et relâchement confirmés sur ROCKER_UP GPIO4 (102553/102937 ms), ROCKER_CLICK GPIO5 (104881/105223 ms), ROCKER_DOWN GPIO6 (106217/106443 ms) et BOOT GPIO0 (113645/113863 ms). Deux cycles BOOT supplémentaires également présents. Les heartbeats continuent jusqu’à 120001 ms : le diagnostic reste actif pendant ce scénario. Ce test valide les entrées directes et leurs événements débouncés ; il ne valide pas les appuis longs fonctionnels, l’enregistrement, le réveil profond ni PWR/PMIC.

Prochaine preuve attendue : lignes I2C 0x34/0x18/0x51/0x6A/0x6B/0x70 du début de journal de démarrage ; présence adresse seulement, pas validation fonctionnelle des composants.
