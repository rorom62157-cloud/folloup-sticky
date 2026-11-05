# Alpha notes sur appareil — 4 novembre 2026

Cette cible `firmware/device/device.ino` est une application testable distincte
des probes de compilation et du diagnostic. Elle ne constitue pas la V1 complète.

## Fonctions de cette alpha

- Notes SD locales : créer avec clavier à molette, consulter avec pagination,
  modifier le texte, épingler/désépingler un post-it, supprimer après confirmation.
- Interface monochrome 800×480, rafraîchissement complet après interaction.
  Attendre la fin du rafraîchissement avant un nouvel appui.
- Haut/bas : choix ; clic : valider ; BOOT : retour. Dans un brouillon non vide,
  sélectionner ANNULER pour abandonner explicitement. En édition, EFFACER retire
  le dernier caractère UTF-8 ; SAUVER conserve l'original sur SD.
- Un titre local par défaut « Note locale » ; modifier le titre depuis le web.
  Les textes longs et caractères hors clavier sont plus faciles à saisir par web.
- Wi-Fi local privé : ouvrir CONNEXION WEB / WI-FI, relever le réseau COMPAGNON-…
  et sa clé aléatoire, puis ouvrir http://192.168.4.1 depuis ce réseau. Pas de
  connexion Internet ni de connexion automatique à un réseau domestique.
  Identifiants renouvelés à chaque démarrage, non stockés dans Git ni les journaux.
- Accord web : demander dans le navigateur puis attendre l'affichage du dialogue,
  relâcher les commandes et faire un nouvel appui central pour accepter, BOOT pour
  refuser. L'accord est lié à la demande affichée. WEB ACTIF indique la session ;
  REVOQUER ACCES WEB la ferme. Pause d'une minute après refus/expiration pour
  éviter que des demandes répétées monopolisent l'écran.
- Sans tableau de bord complet, le web affiche uniquement les vraies notes.

BOOT sert provisoirement de Retour dans cette alpha ; l'enregistrement n'est pas
raccordé. PWR conserve son comportement matériel : maintien prolongé susceptible
de couper l'alimentation. Pas de configuration PMIC/chargeur. Ni veilles,
audio, agenda, Google, Gemini, météo, lecture PDF/EPUB ou autonomie validée.
L'appareil reste éveillé : cette alpha est destinée aux essais, pas à l'autonomie.
Le rendu remplace les accents par leurs lettres de base et les glyphes non pris
en charge par « ? » ; les octets UTF-8 originaux restent conservés sur SD.

## Données et limites

Montage FAT existant en SDMMC 4 bits avec `format_if_mount_failed=false`.
Création du seul répertoire `/sdcard/compagnon-notes`. Les autres fichiers ne sont
ni parcourus ni modifiés. Les notes utilisent les deux snapshots versionnés
existants. Un stockage absent/corrompu ou un fsync en échec bloque les mutations,
sans prétendre avoir sauvegardé et sans formatage automatique.

HTTP et UI partagent un mutex de stockage ; I/O réseau et écran hors de ce mutex.
Les décisions physiques ont un mutex séparé. Une mutation web déjà acceptée peut
terminer après révocation ; les suivantes sont refusées. Clés et paramètres des
services non raccordés ne sont pas enregistrés. Le HTTP local n'est pas chiffré.

## Construction et licence

`sh scripts/build-device.sh` utilise les outils déjà installés, sans mise à jour
ni téléversement. Cible ESP32-S3, flash16M, PSRAM OPI, USB HWCDC, partition default.
Ce Mac : Arduino CLI1.5.0, Arduino-ESP32 3.3.6, ESP-IDF5.5.2, esptool5.1.0.
Les versions 1.5.1/3.3.8/5.5.4/5.2.0 des anciens comptes rendus concernent l'autre
ordinateur. Le script utilise des dossiers de construction neufs pour éviter une
ancienne table de partitions laissée dans le cache.

Le pilote SSD1677 adapte une séquence GPLv3 de la référence épinglée : voir
`device-board-sources.md` et `firmware/device/vendor/FolloUp-LICENSE.txt`.
Distribuer le firmware lié avec sa licence GPLv3 et son code source correspondant.
Ne jamais distribuer une sauvegarde brute, les identifiants Wi-Fi actifs ou des
fichiers SD personnels avec le code.

## Installation et retour

L'installation doit vérifier à nouveau la carte, les protections désactivées,
les images ESP32-S3 et le partitionnement avant écriture. Les images séparées
se trouvent dans `artifacts/device/`. L'image fusionnée n'est jamais utilisée.
La table default doit être identique à celle sur carte ; application ≤0x140000.
La plage 0x0..0x120000 couvre les secteurs des quatre images de cette construction.
La sauvegarde de récupération locale est hors Git sous artifacts ; vérifier sa
somme SHA256 et les comparaisons avec la flash avant installation. Elle n'est pas
une sauvegarde des16Mo complets ni de la SD. Le code n'utilise pas erase-flash.

Après les contrôles, esptool écrit les images séparées : bootloader0x0,
partitions0x8000, boot_app0.bin de ce core à0xe000, application0x10000, puis
verify-flash sur chacune. Conserver les journaux et empreintes exactes localement.
En cas de problème, BOOT maintenu pendant le reset permet la récupération USB ;
relâcher BOOT après entrée ROM et réinstaller/vérifier la sauvegarde des secteurs
sur ce même MCU. Ne pas toucher aux eFuses ni utiliser --force.

## Essais courts

1. Voir le titre MON COMPAGNON - ALPHA NOTES et l'absence de message d'erreur.
2. Créer une courte note avec la molette et SAUVER. Ouvrir la note, épingler,
   puis modifier. Vérifier suppression avec ANNULER avant un essai de suppression.
3. Se connecter au réseau indiqué, demander l'accès web : aucune note avant
   accord sur appareil. Essayer refus, attente, accord, édition et révocation.
4. Vérifier sur appareil une modification faite par web ; ouvrir une ancienne
   édition avant modification distante : conflit, sans écrasement.
5. Redémarrer après sauvegarde et vérifier la persistance de la note.
6. Moniteur USB115200 : envoyer `t` pour le test explicite de stockage. Il crée
   une note temporaire, force les opérations du NoteStore (dont fsync), récupère
   un nouveau store depuis les fichiers, compare le texte et supprime seulement
   cette note test. PASS n'est pas une preuve de résistance aux coupures.

Les résultats observés sur carte sont consignés dans `docs/status.md` ; une
compilation et des tests hôte ne valident pas l'écran ou les gestes physiques.


## Premier passage sur carte

Installation et vérification des quatre images réussies le4novembre2026. La table
présente était identique. Sauvegarde vérifiée des secteurs remplacés conservée
localement horsGit. Le démarrage READY et le test USB `t` sont réellement passés.
Romain confirme le menu MON COMPAGNON - ALPHA NOTES. Les autres gestes de la
procédure ci-dessus restent à valider à l'usage. L'ouverture du moniteur USB peut
redémarrer la carte : consulter les identifiants Wi-Fi affichés après ce reset.
