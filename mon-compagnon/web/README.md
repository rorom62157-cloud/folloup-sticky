# Compagnon web — première tranche

Interface française autonome, sans dépendance ni étape de compilation. Servir `index.html`, `style.css` et `app.js` à la racine de la **même origine** que l’API décrite dans `../docs/contracts.md`. Ouvrir directement le HTML via `file://` ne fournit pas l’API. Aucun mock de données ni accord physique simulé dans le navigateur.

## Disponible

- Demande d’accès et attente d’une décision physique ; fermeture explicite de session.
- Tableau de bord : batterie, réseau, veille, agenda en cache, notes épinglées, stockage et état des intégrations.
- Création, édition, épinglage et suppression confirmée des notes. Les modifications/suppressions envoient la version. Un conflit 409 ne réessaie pas d’écraser : le texte de l’éditeur reste visible, à copier avant de fermer puis comparer à la version actualisée.
- Liste audio ; chargement authentifié avant lecture/téléchargement local par URL objet. Aucune URL audio publique.
- Saisie des paramètres Wi-Fi et de la clé Gemini, sans relecture des secrets. Les champs secrets sont vidés dès l’envoi, y compris en cas d’erreur. Wi-Fi accepté (202) ne signifie pas connexion réussie. Clé enregistrée ne signifie pas clé validée.
- Aide pour les commandes et veilles cibles ; fonctions absentes explicites. Une autorisation manuelle Google via OAuth Playground est possible avec ses propres identifiants ; son adaptateur et sa saisie sécurisée ne sont pas encore implémentés. Aucun appel IA ni contrôle matériel fictif.

## Contrat et confidentialité

Les seules routes consultables avant accord sont `POST /api/session/request` et `GET /api/session`. Aucun accès à `/api/state` avant une réponse `approved` accompagnée de `csrf`. La suppression de session et toutes les écritures privées envoient `X-CSRF-Token`. La demande initiale n’a pas ce jeton ; **le serveur doit valider l’origine et les métadonnées de requête**, y compris pour cette route. Fetch utilise `credentials: same-origin`, `mode: same-origin` et `cache: no-store`.

La session est vérifiée toutes les 3 secondes, l’état toutes les 15 secondes ; aucune route d’activité n’est appelée et ces lectures ne doivent pas prolonger l’inactivité côté serveur. Vérifications en chaîne par temporisateurs, pas d’intervalle accumulant des requêtes. Un identifiant croissant garantit que seule la dernière actualisation peut appliquer son état et programmer le prochain contrôle. Chaque mutation invalide les lectures en cours au début et à la fin ; les callbacks de session précédente ne modifient pas la session suivante, même si le décodage JSON/audio se termine tardivement. Chaque requête a un délai de 12 secondes. Les erreurs d’autorisation 401 et l’échec de vérification de session ferment l’interface, effacent son état privé, retirent les formulaires, annulent les requêtes et temporisateurs, arrêtent l’audio et révoquent les URL objet. `pagehide` effectue aussi ce nettoyage, avec nouvelle vérification lors d’une restauration depuis le cache navigateur.

Une révocation distante est détectée au prochain contrôle ou à la prochaine opération ; elle ne peut pas effacer un fichier déjà téléchargé. Aucun localStorage, sessionStorage, cookie JavaScript, service worker, ressource distante, contenu HTML utilisateur ou journal de secrets. Les textes sont insérés avec `textContent`. Le navigateur garde nécessairement en mémoire le corps d’une requête le temps du transfert et les fichiers audio chargés pendant la session.

Le serveur doit faire respecter l’autorisation sur **toutes** les routes privées et l’audio, les versions, bornes, délais et protections d’origine ; le masquage du DOM n’est pas une sécurité serveur. Cookie HttpOnly SameSite=Strict path=/, jetons imprévisibles d’au moins 128 bits, et absence de secrets dans les réponses selon le contrat. Réponses privées : `Cache-Control: no-store`. Recommandations de livraison : CSP `default-src 'self'; script-src 'self'; style-src 'self'; media-src 'self' blob:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'`, `X-Content-Type-Options: nosniff`. HTTP local n’est pas chiffré ; l’interface le précise avant saisie des secrets.

## Vérification

Sans serveur compatible, seule l’interface de demande et les erreurs réseau fonctionnent. Avec le simulateur hôte fourni séparément : demander puis accepter **dans son terminal**, vérifier les notes/version 409, les erreurs des fonctions indisponibles, le refus/révocation/expiration et l’impossibilité de lire une URL audio sans session. Vérifier également les erreurs réseau, l’accessibilité clavier, le rendu mobile et le nettoyage des champs lors de la fermeture. Les scénarios navigateur ne sont pas déclarés réussis sur la seule base d’une inspection statique.
