# Agenda local et horloge — v0.3

`AgendaStore` est un domaine C++17 local, sans Google ni synchronisation. Son
adaptateur `NoteFiles` doit posséder un répertoire dédié `/sdcard/compagnon-agenda`.
Il réutilise les deux snapshots, sommes de contrôle et transactions de NoteStore,
jamais son répertoire de notes. `recover()` est obligatoire avant utilisation.
Les erreurs I/O bloquent les mutations jusqu'à récupération explicite ; une
création ayant échoué ne doit pas être répétée automatiquement.

32 événements au maximum, titre 160 octets UTF-8 non blanc, détails 4000 octets
UTF-8. Chaque événement possède un identifiant et une version optimiste : update
et erase exigent la version lue. Une date est exactement YYYY-MM-DD, entre 2000
et 2099 avec validation grégorienne ; un horaire est HH:MM. Une journée entière
exige `all_day=true` et `time` vide. Aucune heure implicite. Les événements sont
des dates civiles dans le fuseau configuré, sans conversion à leur stockage.
Un changement de fuseau conserve les heures civiles saisies.

`events()` trie par date, journée entière d'abord, heure, identifiant pour les
égalités. `day(date)` filtre une journée. `upcoming(date,time,limit)` est inclusif,
conserve les événements journée entière du jour et exige une date/heure valide
fournie par le propriétaire. Ces méthodes retournent des copies et rien lorsque
le store n'est pas prêt. L'enveloppe interne `AGENDA1` est fixe et versionnée :
8 octets de signature, date de 10 octets, séparateur, horaire ou `-----`,
séparateur, booléen 0/1, séparateur puis détails UTF-8 bruts. Les détails peuvent
contenir des séparateurs sans ambiguïté. Une note valide avec une enveloppe
incorrecte fait échouer la récupération sans modifier les fichiers.

## Horloge

Le pilote utilise le même bus IDF I2C et le mutex de `audio.hpp`, sans initialiser
un second pilote Wire. La création du bus ne nécessite pas le succès du codec.
Source primaire : [NXP PCF85063A, révision 7.3 du 14 juillet 2026](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf),
sections 7.2.1, 7.3 et 7.4 : adresse 7 bits 0x51, lecture séquentielle des
registres 04h..0Ah, BCD, indicateur OS et mode 24 heures. STOP encadre les écritures.
Les valeurs invalides, OS, STOP et mode 12 heures ne produisent pas d'heure valide.
Aucun appel réseau, aucune alarme RTC ni réveil programmé n'est installé.

`configureClockTimezone` accepte Europe/Madrid (règles POSIX CET/CEST actuelles)
ou UTC. `parseClockLocal` vérifie aussi le passage heure d'été : une minute
inexistante ou répétée est refusée et doit être corrigée par l'utilisateur.
Le système conserve un epoch UTC et `clockSnapshot` restitue une date/heure locale
seulement après une source explicitement validée. Les règles légales futures de
fuseau peuvent exiger une mise à jour du firmware.

`beginClock(rtc_utc_known,error)` ne considère jamais les valeurs d'une RTC
préexistante comme UTC par supposition. La configuration persistée doit attester
que notre application a écrit cette RTC en UTC. Sinon l'utilisateur règle l'heure.
Avant `setClockLocal`, le propriétaire doit persister `rtc_utc_known=false` ;
après succès seulement il peut persister true. Cela empêche d'accepter une date
partielle après coupure. L'heure UTC doit aussi rester entre 2000 et 2099.

`setClockLocal` règle réellement le système puis écrit et relit la RTC. Si la RTC
échoue, l'heure système reste valable pour cette exécution mais la méthode retourne
false avec un message explicite ; ne pas annoncer sa sauvegarde. En cas d'écriture
partielle, le pilote conserve STOP pour empêcher une date partielle de progresser.
Le propriétaire sérialise les appels d'horloge, le fuseau POSIX étant global.

Pas de moteur de rappels dans ce lot. Une date stockée ne constitue pas une alerte
horaire fonctionnelle. Aucun réveil profond ni capacité Google n'est revendiqué.

## Vérification

`./scripts/test-agenda.sh` : CRUD, redémarrage, versions, bornes, tris/filtres,
journées entières, récupération interrompue, enveloppes corrompues, années
bissextiles, BCD RTC, OS et transitions CET/CEST. Tests hôte seulement : présence,
précision, sauvegarde batterie et fonctionnement RTC réel restent à vérifier sur
la carte. Compilation ESP32-S3 distincte, jamais un test matériel ni un flash.

Résultat de cette livraison : tests stricts et ASan/UBSan réussis ; compilation
isolée agenda/horloge ESP32-S3 Arduino-ESP32 3.3.6 réussie, 344283 octets programme,
21568 octets de variables globales. Le smoke utilise un fournisseur de bus factice
uniquement pour l'édition de liens : il ne prouve ni le bus ni la RTC. Aucune image
flashée. L'intégration complète avec le moteur audio est vérifiée par le build
application du propriétaire.
