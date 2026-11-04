# Mon compagnon 0.4 — usages locaux

## Commandes, USB-C vers le bas

- Molette haut/bas : déplacer le choix ou changer de page ; clic : valider.
- Bouton supérieur droit : maintenir pour enregistrer, relâcher pour sauvegarder puis classer. Le démarrage est indiqué à l'écran ; le temps d'initialisation n'est pas enregistré.
- Bouton inférieur droit : retour. Appui long de 1,5 s : **Veille et alimentation**. Choisir **Veille prolongée**, **Éteindre l’appareil** ou **Annuler**. Ce menu est aussi dans Réglages, depuis Aujourd’hui.
- En veille prolongée : clic sur la molette pour réveiller. Le dernier post-it ouvert reste affiché ; à défaut, le premier post-it disponible ou un écran de veille. Wi-Fi et serveur sont arrêtés. Aucun réveil périodique n'est programmé. La veille légère n'est pas encore intégrée.

La veille et l'extinction attendent la fin de l'affichage et refusent une capture, lecture ou sauvegarde audio en cours. Les brouillons suspendus doivent être repris avant de dormir. La coupure matérielle par maintien PWR est désactivée pour permettre l'enregistrement maintenu ; voir audio-v03.md pour la récupération et les bits PMIC concernés.

## Post-it

Dans Compagnon web, choisir **Note** ou **Post-it** avant de saisir. Les post-it acceptent un alignement gauche, centré ou droite et un dessin au doigt ou à la souris, avec annulation du dernier trait et effacement du dessin. Limite explicite : 32 traits et 400 points au total, sur une surface 2:1. La mise en page et le dessin sont conservés séparément du texte original sur la SD.

Sur l'appareil, le titre du post-it est gras. Le titre et le corps diminuent ensemble par paliers, en préservant leur hiérarchie ; un mot court est affiché en grand. Au-delà de la place disponible, haut/bas change de page. Ces tailles automatiques concernent seulement les post-it. L'alignement est également réglable dans les actions du post-it sur l'appareil.

## Autres changements depuis 0.2

Aujourd’hui, cartes et aide illustrée, police proportionnelle, icônes du bandeau agrandies et recentrées. Agenda local avec création/modification/suppression et date/heure RTC. WAV locaux originaux, écoute, volume et gain micro réglables, classement et suppression explicite. **Annuler** quitte le classement sans supprimer l'original audio.

Le Wi-Fi conserve son nom et sa clé, consultables sur l'appareil. Ouvrir http://192.168.4.1 après connexion à ce réseau. À la demande de Romain, l'invitation physique est suspendue : l'accès local repose actuellement sur la clé du réseau, avec contrôles de session/origine/CSRF. La clé et les contenus ne sont pas inclus dans la publication.

L'écran évite les images identiques et utilise le rafraîchissement différentiel après initialisation. Le pilote transmet encore les plans complets ; il ne promet pas un rafraîchissement physique limité à un rectangle. Le nettoyage complet reste disponible pour corriger les traces.

## Vérifications

Les suites hôte couvrent les stockages notes/tâches/agenda/post-it, les limites HTTP et les dessins, les courses de sauvegarde web, le rendu et les transitions réelles du contrôleur. Elles ne prouvent ni le tactile du téléphone, ni le son audible, ni le sommeil matériel. Les résultats de compilation et les observations sur l'appareil sont consignés dans status.md.

Google/Gemini, synchronisation d'agenda, PDF/EPUB, météo/capteurs et veille légère restent à intégrer. L'agenda actuel fonctionne localement ; aucun service externe n'est simulé.
