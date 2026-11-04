# Mon compagnon — Cahier des charges fonctionnel et méthode de développement

Version 1.0 — 4 novembre 2026 — Proposition consolidée soumise à validation.

## 1. Mission et autorisation de démarrage

Créer un assistant personnel de poche sur Waveshare ESP32-S3-ePaper-3.97 : consulter sa journée, capturer des idées et agir sur ses informations personnelles sans retrouver le flot de notifications d’un téléphone. Navigation physique, écran sans tactile, fonctionnement local utile même sans Internet. Un seul compte propriétaire.

Ce document définit le produit et la méthode de réalisation. Le développement, les branches de travail et le déploiement des agents de développement ne commencent qu’après le « go » de Romain. Les études nécessaires à l’implémentation font partie de la première phase après validation.

Le projet Follow Up Sticky / GFollow Sticky indiqué dans le GitHub de Romain est une référence à retrouver précisément. Aucun dépôt, branche ou chemin n’est considéré comme identifié à ce stade. Étudier son ergonomie, ses pilotes et son fonctionnement ; créer une application propre adaptée aux besoins. Toute réutilisation respecte la licence et les attributions. Ne pas écraser le projet de référence.

## 2. Matériel et sources

Cible confirmée par l’utilisateur : Waveshare ESP32-S3-ePaper-3.97, SKU dicté « C33-55-2 ». La documentation constructeur associe la référence à 33552.

Équipement : écran e-paper 800 × 480 de 3,97 pouces, ESP32-S3, Wi-Fi 2,4 GHz, BLE, SHTC3, QMI8658, microphone, haut-parleur et batterie installés, carte microSD de 16 Go. Capacité exacte de batterie non confirmée : ne pas retenir arbitrairement 1000 ou 1500 mAh.

La documentation constructeur consultée indique 8 Mo de PSRAM, 16 Mo de flash, une RTC PCF85063, un codec ES8311, un amplificateur NS4150B et un slot TF FAT32. Elle désigne les boutons latéraux comme BOOT et PWR : leurs contraintes, leur position physique et leur comportement au démarrage doivent être vérifiés dans le schéma et les exemples avant implémentation.

Sources de départ :
- https://docs.waveshare.com/ESP32-S3-ePaper-3.97
- https://docs.waveshare.com/ESP32-S3-ePaper-3.97/Resources-And-Documents
- https://github.com/waveshareteam/ESP32-S3-ePaper-3.97

Produire une référence matérielle sourcée : GPIO, bus partagés, alimentation des périphériques, écran et rafraîchissements, boutons, audio, SD, RTC, capteurs, mesure de batterie et sources de réveil. Aucun brochage supposé. Vérifier la correspondance avec la révision réellement détenue.

Aucune campagne de mesure instrumentée de consommation n’est exigée. Optimiser raisonnablement et observer l’autonomie à l’usage ; ne pas promettre une durée non constatée.

## 3. Architecture et périmètre

Architecture imposée : firmware ESP32 + compagnon web embarqué + services externes Google et météo nécessaires. Aucun serveur personnel, NAS ou hébergement intermédiaire obligatoire.

Le navigateur peut participer à la connexion Google ou aux appels autorisés lorsque la session est ouverte. Étudier cette possibilité sans présumer qu’une page locale contourne OAuth, les restrictions d’origine ou les règles du service. Documenter exactement ce qui fonctionne appareil seul et ce qui exige un navigateur ouvert. Une opération essentielle en arrière-plan ne doit pas dépendre silencieusement d’un onglet qui peut être fermé.

Priorité de la première version : commandes physiques, accueil, notes vocales, post-it, fichiers, deux veilles, compagnon autorisé, Wi-Fi, agenda/tâches/rappels et traitements Gemini, sous réserve de valider les intégrations.

Fonctions prévues par lots complémentaires : PDF/EPUB, météo, capteurs et orientation automatique. Elles restent dans le périmètre ; ne pas les déclarer livrées tant qu’elles ne sont pas testées.

Hors première version : IA locale/Tiny LLM, comptes multiples et livres audio. La lecture de PDF/EPUB est distincte de l’écoute des notes vocales, qui est requise.

## 4. Interface et navigation

Interface en français par défaut. Accueil « Aujourd’hui » : heure/date, prochain rendez-vous, prochaines tâches et rappels, rappels manqués, éléments à traiter et accès aux applications. Afficher immédiatement les données locales, puis les actualiser après synchronisation.

Un écran Applications conserve les tuiles Agenda, Tâches/Rappels, Notes, Post-it, Fichiers, Lecture, Météo/Capteurs et Paramètres. Sélection très visible, retour cohérent, listes paginées ou défilantes sans animations coûteuses. Ne pas imposer le passage par plusieurs tuiles pour une action courante.

Prévoir portrait et paysage, choix manuel et verrouillage. Rotation automatique via QMI8658 après validation, avec stabilisation pour éviter les rotations répétées. Les commandes gardent une sémantique cohérente dans les deux orientations.

Bandeau fin : Wi-Fi, hotspot, état IA, batterie avec pourcentage facultatif, rappel ou notification utile, session compagnon active. Météo/température extérieure et mesures locales sont facultatives et configurables. Distinguer température extérieure et mesure intérieure. Prioriser les informations si l’espace manque.

Aide visuelle : dessin de l’appareil et légendes placées près des commandes, explication des appuis courts/longs, accessible dans Paramètres et proposée au premier démarrage.

## 5. Commandes physiques

Référence de position : USB-C vers le bas.

| Commande | Fonction cible |
|---|---|
| Commande à ressort à gauche, haut/bas | Naviguer, sélectionner et faire défiler |
| Clic central gauche | Valider |
| Bouton supérieur droit maintenu | Enregistrer tant que le bouton reste enfoncé |
| Relâchement du bouton supérieur droit | Arrêter, sauvegarder et ouvrir le classement |
| Bouton inférieur droit, court | Retour ; à l’accueil, aucune action destructive |
| Bouton inférieur droit, long, environ 2–3 s | Ouvrir le choix veille légère / profonde |

Le choix de veille via appui long est la proposition par défaut pour rendre les deux modes accessibles. Selon le retour utilisateur, le projet de référence utilise déjà BOOT et PWR de façon personnalisée : analyser prioritairement cette implémentation, ses pilotes et sa configuration pour reproduire les possibilités utiles. Les désignations BOOT/PWR ne constituent pas à elles seules un blocage. Préserver les commandes souhaitées, rechercher une configuration compatible et ne proposer un changement d’ergonomie qu’en présence d’une contrainte concrètement démontrée, notamment au démarrage ou au réveil.

Le vocal est accessible depuis toutes les vues. Depuis la veille profonde, viser réveil et capture sur le même maintien si le matériel le permet. Signaler le début effectif de capture ; ne pas prétendre enregistrer pendant l’initialisation. Ne pas lancer un second enregistrement si un enregistrement est déjà actif.

Retour sonore activable/désactivable et volume réglable ; indication visuelle toujours présente. Ajouter un mode enregistrement long dans Notes : démarrer/arrêter par clic sans maintien.

## 6. Capture et parcours vocal

Limite : 4 minutes par enregistrement, y compris en mode long pour la première version. Afficher la durée et sauvegarder automatiquement à la limite. La capture et sa sauvegarde fonctionnent sans Internet et sans Gemini.

Enregistrer progressivement sur SD, sans accumuler quatre minutes d’audio en RAM. Définir le format/échantillonnage après validation du codec et de Gemini. Afficher clairement les erreurs de SD ou d’espace avant ou pendant la capture et préserver les données déjà écrites autant que possible.

À l’arrêt, sauvegarder l’original avant toute classification ou traitement. Deux menus successifs sont obligatoires :

| Menu 1 : type | Menu 2 : actions adaptées |
|---|---|
| Note | Garder l’audio, transcription fidèle, texte réorganisé, résumé, brouillon de post-it |
| Agenda | Garder pour plus tard, préparer création de rendez-vous, préparer modification |
| Tâche | Garder pour plus tard, transcription, extraction/création de tâches |
| Rappel | Garder pour plus tard, préparer un rappel daté |
| Post-it | Garder pour plus tard, préparer le texte, préparer puis afficher après validation |
| Supprimer — dernier choix | Confirmer la suppression de l’enregistrement |

Aucun choix ou interruption : conserver dans « À traiter ». Retour depuis le second menu : revenir au choix du type sans perdre l’audio. Les catégories et actions restent accessibles ultérieurement.

Pour une action externe ou un rappel : afficher le récapitulatif et demander validation avant exécution. Montrer titre, date/heure, destination et événement cible en cas de modification. Dates ambiguës ou informations manquantes : demander une précision via sélecteur, saisie ou nouvelle dictée. Ne pas inventer une heure ou un destinataire.

Une demande comportant plusieurs actions produit une liste explicite. Les actions passent par des commandes autorisées et validées par le firmware ; une réponse libre de l’IA ne commande pas directement l’appareil.

## 7. Gemini et mode hors ligne

Clé Gemini personnelle saisissable via compagnon. Sélection du modèle compatible à documenter ; ne pas garantir l’accès gratuit ni un quota permanent.

Fonctions : transcription fidèle, réorganisation, résumé et extraction de données structurées pour actions. Conserver les résultats associés à l’original. Éviter de retraiter un audio déjà traité sans demande.

Distinguer : clé absente, accès Internet indisponible, prêt à essayer, traitement en cours, dernier succès, erreur/quota. Clé enregistrée + Internet ne prouvent pas la validité de la clé.

Vérification réseau légère et limitée, sans appels Gemini destinés uniquement à tester la présence d’Internet. Distinguer connexion Wi-Fi et accès effectif au service. Limiter les relances avec délais croissants.

Hors ligne : enregistrement, classement, lecture locale, édition manuelle, consultation du cache et post-it restent disponibles. Les actions nécessitant l’IA peuvent être enregistrées comme demandes en attente. Proposition par défaut : au retour du réseau, proposer « Traiter les demandes en attente » plutôt que consommer automatiquement le quota. Une action Google reste soumise à validation.

IA locale reportée à une phase ultérieure ; aucune bibliothèque ou modèle local nécessaire au socle.

## 8. Agenda, tâches, rappels et synchronisation

Agenda : vue du jour, prochains événements, détails et cache consultable hors ligne. Google Calendar est la destination privilégiée pour les rendez-vous ; Google Tasks pour les tâches lorsque l’intégration est validée. Rappels horaires locaux pris en charge. Étudier les possibilités de retrouver ces rappels côté Google sans assimiler une date de tâche à une alerte horaire.

Google Keep n’est pas imposé. Les notes/transcriptions doivent rester locales et une voie de synchronisation vers une destination accessible sur téléphone doit être étudiée. Le mécanisme retenu doit éviter un transfert manuel à chaque usage. Toute limitation ou fonction seulement partiellement réalisable doit être exposée avant développement du connecteur.

La connexion Google est distincte de la clé Gemini. Valider OAuth, renouvellement des autorisations et compatibilité avec l’architecture sans serveur personnel. Ne pas utiliser une automatisation fragile de pages Google comme intégration invisible.

Synchroniser à la reprise d’utilisation, sur demande et après une action validée nécessitant l’envoi. Pas de récupération périodique de l’agenda pendant la veille. Un réveil technique d’horloge ne déclenche pas de synchronisation Google.

Afficher dernière synchronisation, état hors ligne et opérations en attente. Conserver identifiants locaux/distants et état des opérations pour éviter les doublons après coupure ou redémarrage. Détecter une modification concurrente avant d’écraser une donnée distante.

Gérer fuseau horaire, heure d’été, événements sur journée entière et dates relatives. Les rappels locaux peuvent alerter en veille légère ; en veille profonde, aucune alerte ni réveil programmé. Les rappels manqués sont regroupés à la reprise dans Aujourd’hui, sans rafale d’alertes.

## 9. Post-it

Inclus dès la première version. Plusieurs notes possibles, une note sélectionnée peut être épinglée à l’écran. Le post-it est une présentation d’une note, pas nécessairement un doublon de son contenu.

Créer/modifier/supprimer depuis dictée + traitement IA, clavier simple à l’écran ou compagnon web. Le clavier à la molette est adapté aux textes courts ; le compagnon facilite les textes longs. Prévoir accents, retour arrière, espaces et validation.

Lisibilité prioritaire : adaptation des tailles, avertissement en cas de texte trop long, absence de troncature silencieuse. Un post-it affiché reste présent lors de la veille légère ou profonde. En profonde, supprimer l’horloge dynamique et les états réseau trompeurs avant de dormir ; aucune actualisation ensuite.

## 10. Deux veilles

| État | Contenu | Réseau et fonctions |
|---|---|---|
| Veille légère automatique | Conserver le contenu | Wi-Fi si activé, compagnon après autorisation, rappels locaux |
| Veille légère manuelle | Horloge par défaut, sauf post-it conservé | Identique |
| Veille profonde avec post-it | Post-it conservé | Réseau coupé, aucun traitement périodique |
| Veille profonde sans post-it | Écran propre « Mon compagnon », petit visuel, batterie au moment de la veille | Identique |

En légère, horloge actualisée par défaut toutes les 2 minutes lorsqu’elle est affichée ; réglage désactivé/1/2/5 minutes proposé. Si l’actualisation est désactivée, masquer l’horloge plutôt que présenter une heure figée. La batterie est actualisée lors des réveils utiles, sans obligation d’un réveil dédié.

Délais depuis la dernière interaction utilisateur :
- Légère : 15 s, 30 s, 1, 2, 5, 7, 10, 15, 20, 30 min.
- Profonde : 1, 2, 5, 10, 15, 20, 30, 60 min ou Jamais.
- Défauts : légère 30 s ; profonde 10 min.
- Profonde strictement après légère lorsqu’activée. Les délais ne s’additionnent pas.

Interactions physiques et actions effectives dans une session compagnon autorisée remettent le compteur à zéro. Le polling web et les tâches techniques ne le remettent pas à zéro. Ne pas entrer en veille en plein enregistrement, sauvegarde, mise à jour ou transfert critique ; utiliser une protection bornée et signaler les erreurs plutôt que rester bloqué indéfiniment.

La veille profonde se termine uniquement par action physique. Préserver le démarrage et le mode de récupération matériel. La stratégie ESP-IDF de veille connectée sera choisie pour satisfaire le comportement, sans confondre basse fréquence CPU et maintien garanti du Wi-Fi.

## 11. Compagnon web et Wi-Fi

Le compagnon est une extension fonctionnelle : configuration, rédaction/édition, correction de textes, gestion des notes/post-it, récupération d’audios, import PDF/EPUB, consultation de l’état de synchronisation et aide.

Mémoriser plusieurs SSID/mots de passe, dont le partage de connexion du téléphone. Prévoir ajout, oubli et reconnexion aux réseaux connus avec tentatives limitées. Wi-Fi désactivable ; hotspot activable manuellement ; coexistence client + hotspot lorsque nécessaire.

Nom local stable souhaité : mon-compagnon.local, avec possibilité de personnalisation et gestion des conflits de nom. Afficher toujours l’IP de secours et les informations du hotspot dans les paramètres. Tester explicitement l’accès depuis le téléphone qui fournit le partage de connexion : ne pas supposer que mDNS et l’accès local se comportent identiquement sur tous les téléphones.

Le hotspot seul ne fournit pas Internet à l’ESP32. Le firmware doit indiquer clairement la différence entre réseau local et Internet disponible. Une utilisation web locale doit rester possible sans Internet.

### Autorisation physique obligatoire

Avant acceptation, aucun accès aux données : ni notes, ni agenda, ni fichiers, ni paramètres, ni URL directe ou API non protégée. Le navigateur reçoit uniquement une interface minimale d’attente.

Demande affichée sur l’appareil ; clic central pour accepter, Retour pour refuser ; expiration sans réponse. L’autorisation est liée à la session du demandeur, pas à l’ensemble du réseau. Icône de session active, expiration et révocation depuis l’appareil. Proposition : une session active à la fois.

Une nouvelle demande ne doit pas interrompre l’enregistrement et ne doit pas pouvoir bloquer l’appareil par répétition. Protéger également les opérations d’écriture contre les requêtes provenant d’autres sites. Ne pas exposer secrets et identifiants dans les logs. Documenter les limites de transport de la solution locale retenue.

Prévenir les écrasements concurrents appareil/compagnon par verrouillage court ou contrôle de version. Depuis la veille profonde, il faut d’abord réveiller physiquement l’appareil.

## 12. Données et SD

Conserver un audio original unique et lui associer les résultats, catégories et actions, sans duplication systématique. Copie indépendante possible pour un export.

Arborescence proposée, à stabiliser dans le contrat de données :
- /compagnon/audio/YYYY/MM/ : originaux identifiés de façon unique.
- /compagnon/notes/ : métadonnées, textes et liens vers les audios.
- /compagnon/cache/ : agenda, tâches, météo.
- /compagnon/queue/ : demandes et opérations en attente.
- /compagnon/books/ : PDF et EPUB.
- /compagnon/exports/ : exports explicites.

Les secrets sont stockés séparément des contenus exportables. Versionner le format des données, prévoir une migration conservant les fichiers, écrire de façon résistante aux interruptions. La SD est du stockage, pas un remplacement de la RAM.

Explorateur : dossiers, ouverture de formats pris en charge, écoute, renommage, suppression avec confirmation et récupération via compagnon. Gestion explicite de SD absente, pleine, retirée ou illisible ; pas de formatage automatique.

Seuil proposé de purge : moins de 10 % libre, soit plus de 90 % occupé. Ce seuil interprète « mémoire limite » ; réglable dans les paramètres. Avertissement non répétitif, aperçu des audios les plus anciens et espace libérable. Protection « À conserver », exclusion des captures en cours et des audios nécessaires à un traitement en attente. Purge automatique uniquement si activée. Préserver les textes dérivés lorsqu’un audio est purgé et indiquer que l’original n’est plus disponible.

## 13. Lecture, météo et capteurs

Lecture : PDF et EPUB uniquement pour le moment. Bibliothèque SD, navigation physique, reprise de position, portrait/paysage ; taille de texte pour les formats reformatables. Tester PDF texte et scannés ainsi que plusieurs EPUB. Documenter les limites réalistes et les éventuels besoins de préparation via compagnon. Ne pas promettre PDF complexe ou DRM sans prise en charge vérifiée.

Météo : localisation manuelle et recherche automatique approximative par service réseau si faisable, jamais GPS présumé. Icône et température extérieure facultatives dans le bandeau, vue détaillée, cache et fraîcheur visible. Mise à jour à l’usage avec cache ; aucun réveil profond pour la météo.

Capteurs : vue température/humidité et affichage facultatif dans le bandeau. QMI8658 pour orientation si validée. Pas d’extension implicite vers le comptage de pas ou d’autres usages.

## 14. Paramètres

Prévoir : Wi-Fi et réseaux mémorisés, hotspot, adresse IP/nom local, session compagnon, Google et calendrier/liste cible, clé/modèle Gemini, fuseau horaire, localisation, retour sonore/volume, batterie avec ou sans pourcentage, bandeau, rendu des gris selon pilote, orientation, délais et affichages de veille, stockage/purge, aide visuelle, versions et diagnostic expurgé des secrets.

Les mises à jour de firmware doivent avoir une procédure documentée et reproductible. Prévoir une voie de récupération USB ; OTA peut être ajoutée si compatible avec le partitionnement retenu. Aucune promesse d’OTA avant validation de l’espace flash et de la récupération.

## 15. Acceptation fonctionnelle

La première version n’est pas considérée validée sur la seule base d’une compilation.

| Scénario | Résultat attendu |
|---|---|
| Appui vocal depuis plusieurs vues | Capture, relâchement, sauvegarde puis deux menus |
| Capture hors ligne de 4 minutes | Audio conservé, arrêt automatique, aucune perte liée à Gemini |
| Aucun choix après capture | Élément retrouvé dans À traiter |
| SD pleine ou absente | Message clair, pas de faux succès ni crash |
| Classification puis résumé | Original conservé et résultat relié |
| Création Google et coupure réseau | État explicite et absence de doublon à la reprise |
| Agenda hors ligne | Cache visible avec date de synchronisation |
| Post-it puis veille profonde | Post-it persistant, aucun rafraîchissement ni réseau |
| Veille profonde hors post-it | Écran Mon compagnon, réveil physique |
| Délais 30 s et 10 min | Calcul depuis la même dernière interaction |
| Accès web non autorisé | Aucune donnée accessible, y compris par API/URL directe |
| Accès web autorisé puis révoqué | Session utilisable puis refusée |
| Téléphone en partage de connexion | Accès testé, nom local ou IP de secours documenté |
| Redémarrage après sauvegarde | Notes, réglages et états d’opérations préservés |
| Rappel passé en profonde | Visible comme manqué à la reprise |
| Lecture | Position conservée sur corpus PDF/EPUB documenté |

Les essais matériels nécessitant une manipulation utilisateur sont fournis comme une courte procédure. Rapporter distinctement tests automatisés, vérifications de compilation et observations réellement effectuées sur l’appareil. Aucune mesure instrumentée d’autonomie n’est une condition d’acceptation.

## 16. Méthode agentique et maîtrise des coûts

Aucun sous-agent Astra. Choisir le modèle et le niveau de raisonnement proportionnellement à la mission. Ne pas affirmer un coût ou une économie chiffrée sans données disponibles.

Allocation initiale indicative, ajustée à la disponibilité effective :

| Rôle | Modèle / thinking proposé |
|---|---|
| Recherche factuelle ciblée, inventaire documentaire | gpt-6-luna / medium |
| Analyse technique de matériel ou authentification | gpt-6.1-sol / medium ; high si nécessaire |
| Conception UI et contrats de données | gpt-6.1-sol / medium |
| Développement firmware, UI et compagnon | gpt-6.1-sol / medium |
| Relecture indépendante, stockage, sécurité des accès | gpt-6.1-sol / high sur périmètre sensible |
| Réconciliation et intégration | gpt-6.1-sol / high si conflits complexes, medium sinon |
| Documentation à partir de changements validés | gpt-6-luna / medium |

Annoncer chaque agent sous la forme « Nom de mission (modèle / thinking) ». Si un modèle n’est pas disponible, indiquer le remplacement non-Astra. Élever le niveau seulement en cas de difficulté identifiée ; ne pas lancer plusieurs modèles pour la même tâche sans raison.

### Contrat de chaque mission

Input obligatoire : objectif borné, exigences concernées, références disponibles, fichiers et branche autorisés, interfaces à respecter, dépendances déjà satisfaites, critères de sortie, vérifications attendues et limites de périmètre.

Output obligatoire : résultat, fichiers/commits concernés, décisions, tests réellement exécutés, limites/blocages et prochaine action nécessaire. Puis fin du tour de l’agent. Aucune boucle d’attente, aucun polling en veille. Une correction nécessite un nouvel input ciblé ; l’identité de l’agent peut être réutilisée sans calcul en attente.

La recherche ne se lance que sur une question précise ; le développement ne se lance que lorsque les informations nécessaires à son périmètre sont disponibles. Un blocage produit un rapport concis, pas une attente active.

### Séquence de travail

1. Recherche parallèle indépendante : matériel et dépôt de référence d’un côté, faisabilité des intégrations sans serveur de l’autre.
2. Synthèse et décisions : architecture, dépendances épinglées, contrats de données/services, budget mémoire et plan de lots. Résoudre les inconnues bloquantes.
3. Spécifications par domaine : UI embarquée, services/stockage et compagnon. Les interfaces partagées sont stabilisées avant développement parallèle.
4. Développement parallèle sur périmètres disjoints : UI, services/firmware et web lorsque leurs inputs sont suffisants. Même fichier partagé : un propriétaire ou intégration séquentielle.
5. Revue indépendante après livrable : défauts concrets avec reproduction, gravité et critères de correction. L’auteur s’arrête pendant la revue.
6. Correction ciblée après retour ; le vérificateur ne tourne pas en attendant. Recontrôler les défauts corrigés et les risques impactés, sans relancer toute la recherche.
7. Réconciliation : assembler les contributions dans une branche d’intégration, résoudre incompatibilités, puis validation globale du résultat intégré.
8. Documentation, livraison et rapport de limites. Nouvelle boucle seulement pour les défauts ou besoins démontrés.

Par défaut, limiter à deux ou trois missions simultanées utiles ; augmenter uniquement si des tâches indépendantes justifient réellement le coût. Ne pas lancer une équipe complète symbolique. Réutiliser les sources et contrats déjà validés plutôt que redistribuer tout l’historique conversationnel.

## 17. Git, documentation et reprise dans un autre IDE

Avant modification, inspecter l’état Git et conserver tout travail utilisateur. Après le go, créer une branche de cadrage/architecture, puis des branches par lot. Utiliser des worktrees isolés quand disponibles et autorisés. Ne pas partager une branche mutable entre agents concurrents.

Commits petits et cohérents, messages explicites, aucune clé/token/audio personnel versionné. Ne pas pousser directement du travail inachevé sur main. Pousser les branches de travail au dépôt convenu, intégrer après revue et contrôles, respecter les protections de branche. Pas de force-push ou de réécriture d’historique par défaut. Ne pas fusionner pendant une vérification sur un état différent ; tests et rapport doivent référencer le commit réellement intégré.

Un rôle documentation intervient par lots sur des changements stabilisés ; il peut travailler en parallèle sur un lot validé pendant que le suivant est développé. Il ne modifie pas les fichiers dont un autre agent est propriétaire. Les auteurs documentent eux-mêmes les contrats, comportements non évidents et raisons des choix ; éviter les commentaires paraphrasant chaque ligne.

Documents à créer dans le dépôt après validation :
- README.md : objectif, matériel, fonctions, installation, compilation/flash, usage et limites.
- AGENTS.md : politique de modèles sans Astra, workflow, propriétaires, conventions, commandes de contrôle et consignes de reprise.
- docs/specification.md : exigences validées et critères de réussite.
- docs/hardware.md : schéma, brochage sourcé, variantes et contraintes.
- docs/architecture.md : modules, dépendances et états de veille.
- docs/contracts.md : formats SD, services, API web, sessions, synchronisation.
- docs/decisions/ : décisions et arbitrages durables.
- docs/testing.md : procédures, résultats, environnement et limites.
- docs/status.md : état courant, commit de référence, terminé/en cours/bloqué et prochaine action.
- docs/agent-worklog.md : missions, modèle/thinking, inputs résumés, outputs, retours et corrections ; aucun raisonnement interne détaillé.
- CHANGELOG.md et README par module lorsque cela aide à comprendre son rôle.

Le point de reprise doit suffire à un autre agent dans l’IDE sans relire ce chat : lire AGENTS.md, README et docs/status.md, vérifier l’état Git, puis reprendre la prochaine mission documentée. Épingler les versions et fournir des commandes reproductibles. L’historique conserve les décisions et preuves utiles, pas un journal verbeux de chaque appel d’outil.

## 18. Première étape après le go

Retrouver et confirmer le dépôt utilisateur, examiner l’existant sans l’écraser, produire le tableau matériel depuis les sources constructeur et tester la faisabilité des connexions Google sans serveur personnel. Présenter les seuls écarts ou blocages réels, puis démarrer les lots dont les prérequis sont satisfaits.

Ne pas demander de nouveau les décisions déjà validées. Une incompatibilité matérielle ou une intégration impossible impose un arbitrage explicite, pas une fonctionnalité factice. Les valeurs indiquées comme propositions sont des defaults ajustables et doivent rester identifiées dans la spécification jusqu’à validation.
