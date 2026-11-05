# Mon compagnon 0.2 — portrait et applications locales

Le portrait dans le sens corrigé après retour visuel de Romain est le réglage initial. Accueil donne accès à Applications, Nouvelle
note, Nouvelle tâche, Mes post-it et Compagnon web. Le menu Applications rassemble
Notes, Post-it, Tâches, Fichiers texte, Compagnon web, Réglages et Aide.

## Commandes

Haut/bas sélectionnent ; maintenir fait défiler. Clic ouvre ou confirme ; BOOT
revient à l'écran précédent. Pendant un rafraîchissement, les déplacements sont
regroupés vers le dernier choix. Une validation est mise en attente jusqu'à ce
que ce choix soit effectivement affiché ; un nouveau déplacement l'annule.
Le clavier propose minuscules, majuscules, chiffres/ponctuation et accents.
Les accents sont conservés en UTF-8 ; le rendu les simplifie, avec une légende
pour distinguer les touches accentuées. SUIVANT passe du titre au texte ; SAUVER
enregistre. BOOT ou ANNULER demande si le brouillon doit être abandonné.

## Usages disponibles

- Notes : titre et texte, création, lecture paginée, renommage, modification,
  épinglage/désépinglage, suppression avec confirmation sur Annuler par défaut.
- Post-it : liste dédiée des notes épinglées et création directe d'un post-it.
  La création et l'épinglage sont deux transactions : en cas d'échec du second,
  la note reste sauvée et l'erreur d'épinglage est annoncée.
- Tâches locales : titre, détails, cocher/décocher, renommer, modifier et supprimer.
  Les versions et l'état terminé survivent au redémarrage. Stockage dédié
  `/sdcard/compagnon-tasks`, distinct de `/sdcard/compagnon-notes`. Aucun compte
  Google ni rappel daté n'est associé à ces tâches.
- Fichiers texte : navigation dans les dossiers de la SD et lecture seule de
  fichiers TXT/MD UTF-8 jusqu'à64Kio, avec pagination. Pas de suppression ni
  d'écriture par ce lecteur. Répertoires privés/masqués exclus, chemins bornés,
  refus des données binaires ou invalides. Limites explicites64entrées/dossier
  et4096noms parcourus ; aucune liste tronquée silencieusement. PDF/EPUB absents.
- Compagnon web : réseau local protégé, notes consultables/modifiables après
  accord physique, révocation et coupure/réactivation Wi-Fi. Une panne réseau
  ne désactive pas les notes, tâches et fichiers locaux. Les tâches ne sont pas
  encore exposées au compagnon web.
- Réglages : portrait, portrait inversé ou paysage, choix mémorisé ; nettoyage
  complet de l'écran ; lecture des capacités libres/totales réelles de la SD.
- Aide : commandes et fonctions, lecture paginée directement sur l'appareil.

Le rendu est adapté aux480×800 pixels logiques du portrait. Les deux buffers
résident en PSRAM ; une tâche gère l'écran sans bloquer la lecture des boutons.
Les changements utilisent le rafraîchissement complet rapide vérifié dans la
référence SSD1677, avec un rafraîchissement normal périodique ou à la demande.
Le dialogue d'accord web n'est armé qu'après sa fin d'affichage, relâchement des
boutons et nouvel appui ; il reste lié à l'identifiant de la demande. Une panne
terminale d'écran révoque l'accès web et coupe le point d'accès.

## Essais courts sur l'appareil

1. Vérifier le portrait. Si la lecture est retournée, Réglages > Orientation
   propose Portrait inversé. Le choix est mémorisé.
2. Applications > Notes > Nouvelle note : titre, SUIVANT, texte, SAUVER. Clic dans
   la note ouvre ses actions ; essayer Renommer puis Épingler et afficher.
3. Applications > Tâches : créer une tâche, ouvrir ses actions, Marquer terminée.
   Redémarrer et vérifier que la note, la tâche et son état sont conservés.
4. Applications > Fichiers texte : ouvrir un dossier puis un TXT/MD, Haut/Bas
   changent la page, BOOT revient au dossier. Les originaux ne sont pas modifiés.
5. Compagnon web : connecter son navigateur au réseau et à l'adresse affichés,
   demander l'accès puis accepter sur appareil. Tester ensuite la révocation.

USB115200 : `t` exécute deux essais de stockage avec des éléments temporaires :
création/récupération/relecture/suppression d'une note et création/cochage/
récupération/suppression d'une tâche. Les journaux n'affichent aucun contenu
personnel. Une erreur conserve l'élément temporaire et nécessite une inspection,
sans répétition automatique ni formatage. Ce n'est pas un test de coupure SD.

## Construction et limites

`sh scripts/build-device.sh` utilise Arduino-ESP323.3.6 sur ce Mac et produit les
images ESP32-S3/16MB/PSRAMOPI. Le pilote lié reste GPLv3 ; provenance et licence
conservées, voir device-board-sources.md et firmware/device/vendor/.

Cette version est utilisable pour les fonctions locales ci-dessus. Audio,
Google/Gemini, agenda, rappels datés, PDF/EPUB, météo et veilles restent à intégrer.
L'appareil reste éveillé ; aucune autonomie n'est revendiquée. Batterie non
mesurée et aucune reconfiguration PMIC/chargeur. PWR garde sa coupure matérielle.
Les résultats observés, versions, contrôles et installation sont dans status.md.


## Résultat du premier passage0.2

Application installée et verify-flash réussi. Démarrage READY0.2 et essais
NOTES_TEST PASS / TASKS_TEST PASS lus sur la carte. Après correction du sens,
Romain confirme le portrait à l'endroit et l'ouverture du menu Applications.
Les essais de navigation détaillée, lecteur texte et parcours navigateur restent
à faire à l'usage ; aucune validation de ces parcours n'est déduite du seul menu.
