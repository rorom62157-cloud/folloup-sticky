# Réseau et écran — version 0.3

La politique d’application peut désormais choisir `PairingMode::LocalPassword`
avec une attestation explicite de réseau AP protégé par mot de passe, au
constructeur d’`EmbeddedHttp`. Sans cette attestation, le démarrage est refusé.
Le défaut reste `Physical`, compatible avec les anciens appelants. Cette option
est l’exception demandée pour les tests : la validation physique est suspendue,
et l’interface annonce ce mode réel. Elle ne prétend pas qu’un clic a eu lieu.

Le POST de démarrage doit toujours porter l’Origin exact et du JSON `{}`. Le
cookie aléatoire HttpOnly/SameSite Strict lie une seule session au navigateur ;
le CSRF est délivré au statut de cette session approuvée. Les routes privées
restent interdites sans cookie autorisé. L’inactivité de dix minutes, la
révocation locale, les vérifications Host/Origin/Fetch Metadata et les limites
de requête restent appliquées. Le polling ne prolonge pas la session.

## Chargement HTTP

Le code ancien réduisait les sept connexions du SDK à trois avec purge LRU :
les connexions spéculatives et les chargements parallèles HTML/CSS/JavaScript
pouvaient se concurrencer et provoquer une éviction. Le serveur retrouve le
budget de sept et termine chaque réponse par fermeture de connexion explicite.
Les assets sont envoyés par blocs HTTP de 1 024 octets avec bloc terminal ; les
réponses JSON conservent leur longueur déclarée. Aucun mutex session ou
stockage n’est détenu pendant les envois.

L’examen du SDK Arduino-ESP32 3.3.6 confirme port 80 et sept sockets par défaut,
huit routes disponibles et seize sockets lwIP. Quatre routes wildcard par
méthode sont enregistrées ; ce nombre reste dans la limite. Les douze en-têtes
de réponse configurés couvrent les sept au plus utilisés. Aucun de ces points
ne prouve à lui seul la cause du chargement infini rapporté sur l’appareil.
Les nouveaux journaux indiquent uniquement port, mode, statut, nombre d’octets
et résultat d’envoi, sans URL, identifiant, contenu, cookie ou secret. La preuve
reste un chargement réel ou le test HTTP boucle locale sur le firmware installé.
Une réponse asset utilise Transfer-Encoding chunked : comparer les octets
reçus à sa taille embarquée, et non à Content-Length absent.

## Différentiel SSD1677

La source vérifiée est le [pilote FolloUp épinglé](https://github.com/rorom62157-cloud/folloup-sticky/blob/1682b7e7d7ff9c6b23ab7e903c44b241f095b402/components/epaper_panel/ssd1677_driver.cpp),
blob `cd7018d06e97ccaa596bd8eab621dfcd3c9b9dcb` : `InitPartial`,
`RefreshPartialFullScreen`, `TurnOnDisplayPart` et `RefreshChangedRegion`.
La licence GPLv3 et l’attribution des fichiers adaptés sont conservées.

`display(frame,error,false)` établit/nettoie la base par F7. Après succès,
`display(frame,error,true)` emploie réellement FF différentiel, sans reset,
avec l’ancien plan 0x26 et le nouveau plan 0x24. L’image précédente est conservée
dans 48 000 octets de PSRAM et copiée seulement après succès du BUSY. Une image
identique est ignorée. Une erreur invalide cette base et le prochain essai
rétablit une base normale F7. Aucun nettoyage périodique n’est imposé par le
pilote : l’application décide du nettoyage explicite.

Les deux plans sont écrits sur toute la surface. La source précise que les
fenêtres SSD1677 limitent les écritures RAM, jamais la zone activée sur le verre ;
un rectangle de RAM isolé peut donc réactiver d’anciennes zones. Le différentiel
ne déplace que les pixels ancien/nouveau différents. Aucun LUT, inversion ou
commande de rafraîchissement par rectangle n’est inventé. L’accumulation de
rémanence et le contraste restent à observer sur l’appareil.

## Vérification

Les tests C++ de frontière HTTP couvrent les deux modes, le refus sans AP
authentifié, Origin/CSRF/cookie, expiration et révocation. Les régressions Node
couvrent le passage de l’approbation locale à la récupération du CSRF et au
rendu privé sans écran d’attente physique. Ces tests utilisent un transport
contrôlé, pas un navigateur ni le Wi-Fi matériel. `board.cpp` et le transport
HTTP sont compilés strictement avec le SDK réel 3.3.6, `-Wall -Wextra -Werror`
et C++17. Aucun flash ni résultat visuel physique n’est revendiqué dans ce lot.
