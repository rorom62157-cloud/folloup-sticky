'use strict';
(() => {
  const app = document.getElementById('app');
  let csrf = null, state = null, generation = 0, sessionTimer = null, dataTimer = null;
  let view = 'today', editing = null, busy = false, refreshId = 0;
  let requestingAccess = false;
  // Keep byte limits aligned with core/http_access.cpp and core/note_store.hpp.
  const MAX_BODY_BYTES = 24576, MAX_TITLE_BYTES = 160, MAX_TEXT_BYTES = 4096;
  const utf8Bytes = value => new TextEncoder().encode(value).byteLength;
  const requests = new Set(), mediaURLs = new Set();
  const node = (tag, text, attrs = {}) => {
    const el = document.createElement(tag);
    if (text !== null && text !== undefined) el.textContent = text;
    for (const [key, value] of Object.entries(attrs)) el.setAttribute(key, value);
    return el;
  };
  function button(text, fn, kind = '') {
    const el = node('button', text, { type: 'button', class: kind });
    el.addEventListener('click', fn); return el;
  }
  function put(parent, ...children) { parent.append(...children); return parent; }
  function clearMedia() {
    app.querySelectorAll('audio').forEach(a => { a.pause(); a.removeAttribute('src'); a.load(); });
    for (const url of mediaURLs) URL.revokeObjectURL(url);
    mediaURLs.clear();
  }
  function cleanup() {
    generation++; invalidateRefresh(); clearTimeout(sessionTimer);
    requestingAccess = false;
    sessionTimer = dataTimer = null;
    for (const controller of requests) controller.abort();
    requests.clear(); clearMedia();
  }
  function invalidateRefresh() {
    refreshId++; clearTimeout(dataTimer); dataTimer = null;
  }
  function beginMutation() {
    invalidateRefresh(); busy = true; return generation;
  }
  function finishMutation(ownGeneration) {
    if (ownGeneration !== generation) return false;
    invalidateRefresh(); busy = false; return Boolean(csrf);
  }
  class ApiError extends Error {
    constructor(message, status) { super(message); this.status = status; }
  }
  async function api(path, options = {}) {
    const body = options.body === undefined ? undefined : JSON.stringify(options.body);
    if (body !== undefined && utf8Bytes(body) > MAX_BODY_BYTES) {
      throw new ApiError('Le contenu envoyé dépasse 24 576 octets après encodage. Réduisez le texte puis réessayez ; rien n’a été enregistré.', 413);
    }
    const ownGeneration = generation;
    const controller = new AbortController(); requests.add(controller);
    const timeout = setTimeout(() => controller.abort(), 12000);
    const headers = { Accept: 'application/json' };
    const method = options.method || 'GET';
    if (options.body !== undefined) headers['Content-Type'] = 'application/json';
    if (method !== 'GET' && csrf) headers['X-CSRF-Token'] = csrf;
    try {
      const res = await fetch(path, { method, headers, credentials: 'same-origin', mode: 'same-origin', cache: 'no-store', signal: controller.signal, body });
      if (ownGeneration !== generation) throw new DOMException('Session fermée', 'AbortError');
      if (res.status === 401) { locked('Votre session est fermée. Autorisez à nouveau l’accès sur l’appareil.'); throw new ApiError('Session fermée.', 401); }
      if (!res.ok) {
        let message = `La demande a échoué (${res.status}).`;
        try { const detail = await res.json(); if (typeof detail.message === 'string') message = detail.message; } catch (_) { /* Non-JSON errors stay generic. */ }
        if (ownGeneration !== generation) throw new DOMException('Session fermée', 'AbortError');
        throw new ApiError(message, res.status);
      }
      const result = res.status === 204 ? null : options.audio ? await res.blob() : await res.json();
      if (ownGeneration !== generation) throw new DOMException('Session fermée', 'AbortError');
      return result;
    } finally { clearTimeout(timeout); requests.delete(controller); }
  }
  const errorText = error => error.name === 'AbortError' ? 'Connexion interrompue ou délai dépassé. Réessayez.' : error.message;
  function feedback(message, error = false) {
    const el = document.getElementById('feedback');
    if (el) { el.textContent = message; el.className = `notice${error ? ' error' : ''}`; }
  }
  function locked(message = '', pending = false) {
    cleanup(); csrf = null; state = null; editing = null; busy = false;
    // Remove all forms and personal content, including their event handler closures.
    app.replaceChildren();
    const box = node('section', null, { class: 'lock', 'aria-labelledby': 'lock-title' });
    const art = node('div', null, { class: 'lock-art', 'aria-hidden': 'true' });
    put(art, node('div', 'm.', { class: 'lock-screen' }));
    put(box, art, node('span', 'Un accès, votre accord', { class: 'eyebrow' }), node('h1', pending ? 'Un clic sur votre compagnon.' : 'Votre espace, en confiance.', { id: 'lock-title' }), node('p', pending ? 'La demande est en attente. Sur l’appareil, utilisez le clic central pour accepter ou Retour pour refuser.' : 'Retrouvez vos notes et vos audios, rédigez un post-it et réglez votre compagnon. L’accès commence par votre accord physique.', { class: 'muted' }));
    const notice = node('p', message, { id: 'feedback', role: 'status', class: 'notice' });
    box.append(notice);
    const actions = node('div', null, { class: 'actions' });
    if (!pending) actions.append(button('Demander l’accès', requestAccess, 'primary'));
    put(box, actions, node('p', pending ? 'La demande expire après 60 secondes. Aucune donnée personnelle n’est encore accessible.' : 'Réveillez d’abord l’appareil s’il est en veille profonde.', { class: 'small muted' }));
    app.append(box);
    if (pending) scheduleSession();
  }
  async function requestAccess(event) {
    if (busy || requestingAccess) return;
    // A bootstrap GET may still describe the session before this POST. Its
    // response must never lock the UI and abort the new access request.
    cleanup(); requestingAccess = true; busy = true;
    const ownGeneration = generation;
    const btn = event.currentTarget; btn.disabled = true;
    try { await api('/api/session/request', { method: 'POST', body: {} }); if (ownGeneration === generation) locked('', true); }
    catch (error) { if (ownGeneration === generation && error.status !== 401) { feedback(errorText(error), true); btn.disabled = false; } }
    finally { if (ownGeneration === generation) { requestingAccess = false; busy = false; scheduleSession(); } }
  }
  function scheduleSession() {
    clearTimeout(sessionTimer);
    sessionTimer = setTimeout(checkSession, 3000);
  }
  async function checkSession() {
    if (requestingAccess) return;
    let ownGeneration = generation;
    try {
      const session = await api('/api/session');
      if (generation !== ownGeneration) return;
      if (session.state === 'approved' && typeof session.csrf === 'string' && session.csrf) {
        if (!csrf) { cleanup(); ownGeneration = generation; csrf = session.csrf; buildPrivate(); await refresh(); }
        else csrf = session.csrf;
        if (ownGeneration === generation && csrf) scheduleSession();
      } else if (session.state === 'pending') {
        if (!document.querySelector('.lock')) locked('', true); else scheduleSession();
      } else locked(session.state === 'denied' ? 'La demande a été refusée sur l’appareil.' : 'Aucune session active. Vous pouvez demander l’accès.');
    } catch (error) {
      if (generation !== ownGeneration || error.status === 401) return;
      // Fail closed on loss of the authorization check; do not leave cached private data visible.
      locked(`Impossible de vérifier l’autorisation. ${errorText(error)}`);
    }
  }
  function buildPrivate() {
    app.replaceChildren();
    const heading = node('div', null, { class: 'heading' });
    put(heading, put(node('div'), node('span', 'Votre espace personnel', { class: 'eyebrow' }), node('h1', 'Bonjour, on fait le point ?')));
    put(heading, put(node('div', null, { class: 'toolbar' }), button('Actualiser', () => refresh(true)), button('Fermer la session', closeSession)));
    app.append(heading, node('p', '', { id: 'feedback', role: 'status', class: 'notice' }));
    const nav = node('nav', null, { class: 'tabs', 'aria-label': 'Rubriques' });
    for (const [id, title] of [['today', 'Aujourd’hui'], ['notes', 'Notes & post-it'], ['audio', 'Enregistrements'], ['settings', 'Paramètres'], ['help', 'Aide']]) {
      const btn = button(title, () => showView(id)); btn.dataset.view = id; nav.append(btn);
    }
    app.append(nav, node('section', null, { id: 'content', 'aria-label': 'Contenu de la rubrique' })); showView(view);
  }
  function showView(next) {
    if (busy) return;
    clearMedia(); view = next; editing = null;
    app.querySelectorAll('[data-view]').forEach(b => b.setAttribute('aria-current', b.dataset.view === view ? 'page' : 'false'));
    render();
  }
  async function closeSession() {
    if (busy) return;
    const ownGeneration = beginMutation();
    try { await api('/api/session', { method: 'DELETE' }); if (ownGeneration === generation) locked('Session fermée.'); }
    catch (error) { if (ownGeneration === generation && csrf) feedback(errorText(error), true); }
    finally { if (finishMutation(ownGeneration)) await refresh(); }
  }
  async function refresh(force = false) {
    if (!csrf || busy) return;
    clearTimeout(dataTimer); dataTimer = null;
    const ownGeneration = generation, ownRefreshId = ++refreshId;
    try {
      const next = await api('/api/state');
      if (generation !== ownGeneration || ownRefreshId !== refreshId) return;
      const firstLoad = state === null;
      state = next;
      // Never replace an open editor or secret fields during background refresh.
      if (!busy && !editing && view !== 'settings' && (view !== 'audio' || firstLoad || force)) {
        if (view === 'audio') clearMedia();
        render();
      }
    } catch (error) { if (generation === ownGeneration && ownRefreshId === refreshId && csrf) feedback(errorText(error), true); }
    finally { if (generation === ownGeneration && ownRefreshId === refreshId && csrf) dataTimer = setTimeout(refresh, 15000); }
  }
  function card(title) { return put(node('section', null, { class: 'card' }), node('h2', title)); }
  function empty(parent, message) { parent.append(node('p', message, { class: 'empty' })); }
  const fmtBytes = value => Number.isFinite(value) ? `${(value / 1024 / 1024).toLocaleString('fr-FR', { maximumFractionDigits: 1 })} Mo` : 'Inconnu';
  const googleStatus = value => ({ not_configured: 'Non configuré', pending: 'En attente', ready: 'Connecté', error: 'Erreur' }[value] || 'Inconnu');
  function render() {
    const content = document.getElementById('content'); if (!content) return;
    content.replaceChildren();
    if (view === 'settings') { settings(content); return; }
    if (view === 'help') { help(content); return; }
    if (!state) { empty(content, 'Chargement des données autorisées…'); return; }
    if (view === 'notes') { notes(content); return; }
    if (view === 'audio') { recordings(content); return; }
    const stats = node('div', null, { class: 'stats' });
    for (const [title, value] of [['Batterie', state.battery_percent === null ? 'Non mesurée' : `${state.battery_percent} %`], ['Internet', state.network.internet ? 'Disponible' : 'Hors ligne'], ['Compagnon', state.sleep === 'light' ? 'Veille légère' : 'Éveillé']]) put(stats, put(node('div', null, { class: 'stat' }), node('small', title), node('strong', value)));
    content.append(stats);
    const grid = node('div', null, { class: 'grid' });
    const agenda = card('La journée, en clair');
    if (!state.agenda.length) empty(agenda, 'Aucun rendez-vous disponible dans le cache local.');
    for (const event of state.agenda) {
      const date = new Date(event.start);
      put(agenda, put(node('article', null, { class: 'item' }), node('h3', event.title), node('p', Number.isNaN(date.getTime()) ? String(event.start) : date.toLocaleString('fr-FR'), { class: 'muted small' })));
    }
    const pinned = card('À garder sous les yeux');
    const pins = state.notes.filter(n => n.pinned);
    if (!pins.length) empty(pinned, 'Épinglez une note depuis Notes & post-it.');
    pins.forEach(n => put(pinned, put(node('article', null, { class: 'item' }), node('h3', n.title || 'Sans titre'), node('p', n.text, { class: 'note-text' }))));
    const status = card('Tout reste à sa place'); status.classList.add('wide');
    put(status, node('p', `Stockage : ${fmtBytes(state.storage.free_bytes)} libres sur ${fmtBytes(state.storage.total_bytes)}. Réseau : ${state.network.ssid || 'Non connecté'}.`, { class: 'muted' }), node('p', `Google : ${googleStatus(state.google.state)} · Gemini : ${state.gemini.configured ? 'clé enregistrée, validité non vérifiée' : 'clé absente'}.`, { class: 'small' }));
    put(grid, agenda, pinned, status); content.append(grid);
  }
  function notes(content) {
    put(content, put(node('div', null, { class: 'heading' }), node('p', 'Les petites idées méritent une place.', { class: 'muted' }), button('Nouvelle note', () => editNote(null), 'primary')));
    const list = card('Vos notes');
    const sorted = [...state.notes].sort((a, b) => Number(b.pinned) - Number(a.pinned));
    if (!sorted.length) empty(list, 'Aucune note pour le moment. Créez votre première idée.');
    for (const note of sorted) {
      const item = node('article', null, { class: 'item' });
      if (note.pinned) item.append(node('span', 'Épinglée', { class: 'pill pinned' }));
      put(item, node('h3', note.title || 'Sans titre'), node('p', note.text, { class: 'note-text' }));
      const actions = node('div', null, { class: 'actions' });
      put(actions, button('Modifier', () => editNote(note)), button(note.pinned ? 'Désépingler' : 'Épingler', () => changeNote(note, { pinned: !note.pinned })), button('Supprimer', () => deleteNote(note), 'danger'));
      item.append(actions); list.append(item);
    }
    content.append(list, node('div', null, { id: 'editor', class: 'editor' }));
  }
  function field(form, title, id, type = 'text', value = '', maxLength = null) {
    const input = node(type === 'textarea' ? 'textarea' : 'input', null, { id, name: id });
    if (type !== 'textarea') input.type = type;
    if (maxLength) input.maxLength = maxLength;
    input.value = value; put(form, node('label', title, { for: id }), input); return input;
  }
  function editNote(note) {
    if (busy) return;
    editing = note ? { ...note } : { fresh: true };
    const host = document.getElementById('editor'); host.replaceChildren();
    const box = card(note ? 'Modifier votre note' : 'Une nouvelle idée');
    const form = node('form');
    const title = field(form, 'Titre', 'note-title', 'text', note?.title || '', MAX_TITLE_BYTES);
    const text = field(form, 'Texte', 'note-text', 'textarea', note?.text || '', MAX_TEXT_BYTES); text.required = true;
    form.append(node('p', 'Limites : titre de 160 octets et texte de 4 096 octets en UTF-8. Les accents et emoji peuvent occuper plusieurs octets.', { class: 'small muted' }));
    const pin = node('input', null, { type: 'checkbox', id: 'note-pin' }); pin.checked = note?.pinned || false;
    if (note) put(form, put(node('label', null, { class: 'checkbox', for: 'note-pin' }), pin, node('span', 'Épingler comme post-it')));
    const save = node('button', 'Enregistrer', { type: 'submit', class: 'primary' });
    put(form, put(node('div', null, { class: 'actions' }), save, button('Annuler', () => { if (!busy) { editing = null; render(); } })));
    form.addEventListener('submit', async event => {
      event.preventDefault(); if (busy) return;
      const data = { title: title.value, text: text.value };
      if (utf8Bytes(data.title) > MAX_TITLE_BYTES || utf8Bytes(data.text) > MAX_TEXT_BYTES) {
        feedback('Note trop longue : le titre est limité à 160 octets et le texte à 4 096 octets en UTF-8. Réduisez le contenu ; votre texte reste dans l’éditeur et rien n’a été enregistré.', true);
        return;
      }
      if (note) { data.pinned = pin.checked; data.version = note.version; }
      const ownGeneration = beginMutation(); save.disabled = true;
      try {
        await api(note ? `/api/notes/${encodeURIComponent(note.id)}` : '/api/notes', { method: note ? 'PATCH' : 'POST', body: data });
        if (ownGeneration !== generation) return;
        editing = null; feedback('Note enregistrée.'); render();
      } catch (error) {
        if (ownGeneration === generation && csrf) {
          feedback(error.status === 409 ? 'Cette note a changé sur l’appareil. Votre texte reste ici : copiez-le avant de fermer cet éditeur, puis rechargez la note pour comparer. Aucune version n’a été écrasée.' : errorText(error), true);
          if (error.status === 409) { save.disabled = true; save.textContent = 'Conflit : recharger la note'; }
          else save.disabled = false;
        }
      } finally { if (finishMutation(ownGeneration)) await refresh(); }
    });
    put(box, form); host.append(box); host.scrollIntoView({ behavior: 'smooth', block: 'start' }); title.focus();
  }
  async function changeNote(note, patch) {
    if (busy) return;
    const ownGeneration = beginMutation();
    try { await api(`/api/notes/${encodeURIComponent(note.id)}`, { method: 'PATCH', body: { title: note.title, text: note.text, pinned: note.pinned, version: note.version, ...patch } }); if (ownGeneration === generation) feedback('Note mise à jour.'); }
    catch (error) { if (ownGeneration === generation && csrf) feedback(error.status === 409 ? 'La note a changé. La liste va être actualisée ; vérifiez-la avant de réessayer.' : errorText(error), true); }
    finally { if (finishMutation(ownGeneration)) await refresh(); }
  }
  async function deleteNote(note) {
    if (busy || !window.confirm(`Supprimer la note « ${note.title || 'Sans titre'} » ? Cette action est définitive.`)) return;
    const ownGeneration = beginMutation();
    try { await api(`/api/notes/${encodeURIComponent(note.id)}?version=${encodeURIComponent(note.version)}`, { method: 'DELETE' }); if (ownGeneration === generation) feedback('Note supprimée.'); }
    catch (error) { if (ownGeneration === generation && csrf) feedback(error.status === 409 ? 'La note a changé. Suppression annulée ; consultez sa version actuelle.' : errorText(error), true); }
    finally { if (finishMutation(ownGeneration)) await refresh(); }
  }
  function recordings(content) {
    content.append(node('p', 'Les originaux audio sont conservés avant tout traitement. Leur lecture nécessite votre session autorisée.', { class: 'section-intro muted' }));
    const list = card('Vos enregistrements');
    if (!state.recordings.length) empty(list, 'Aucun enregistrement disponible. La capture se fait sur l’appareil.');
    for (const recording of state.recordings) {
      const item = node('article', null, { class: 'item' });
      put(item, node('h3', recording.title || 'Enregistrement'), node('span', recording.category, { class: 'pill' }), node('span', recording.status, { class: 'pill' }));
      const load = button('Charger l’audio', async () => {
        load.disabled = true; const ownGeneration = generation;
        try {
          const blob = await api(`/api/recordings/${encodeURIComponent(recording.id)}/audio`, { audio: true });
          if (!csrf || ownGeneration !== generation || !item.isConnected) return;
          const url = URL.createObjectURL(blob); mediaURLs.add(url);
          const player = node('audio', null, { controls: '', preload: 'none' }); player.src = url;
          const download = node('a', 'Télécharger l’original', { href: url, download: `compagnon-${String(recording.id).replace(/[^a-zA-Z0-9_-]/g, '_')}.audio`, class: 'small' });
          put(item, player, download); load.remove();
        } catch (error) { if (ownGeneration === generation && csrf && item.isConnected) { feedback(errorText(error), true); load.disabled = false; } }
      });
      item.append(put(node('div', null, { class: 'actions' }), load)); list.append(item);
    }
    content.append(list, node('p', 'Le classement, la suppression et le traitement Gemini ne sont pas disponibles dans cette première interface.', { class: 'notice' }));
  }
  function settings(content) {
    content.append(node('p', 'Les secrets sont envoyés une seule fois à votre compagnon et ne sont jamais relus par cette interface. Sur un réseau local en HTTP, le transport n’est pas chiffré.', { class: 'section-intro muted' }));
    const grid = node('div', null, { class: 'grid' });
    const wifi = card('Se connecter au Wi-Fi');
    const wf = node('form');
    const ssid = field(wf, 'Nom du réseau (SSID)', 'wifi-ssid', 'text', '', 32); ssid.required = true; ssid.autocomplete = 'off';
    const password = field(wf, 'Mot de passe (vide pour un réseau ouvert)', 'wifi-password', 'password', '', 128); password.autocomplete = 'new-password';
    secretForm(wf, '/api/settings/wifi', () => ({ ssid: ssid.value, password: password.value }), [password], 'Configuration Wi-Fi acceptée. La connexion peut interrompre cette session ; son résultat reste à vérifier.', 'Enregistrer le réseau');
    put(wifi, wf, node('p', 'La gestion des réseaux mémorisés et du hotspot sera ajoutée avec l’adaptateur matériel.', { class: 'small muted' }));
    const gemini = card('Une aide avec Gemini'); const gf = node('form');
    const key = field(gf, 'Votre clé API Gemini', 'gemini-key', 'password', '', 512); key.required = true; key.autocomplete = 'new-password'; key.spellcheck = false;
    secretForm(gf, '/api/settings/gemini', () => ({ api_key: key.value }), [key], 'Clé Gemini enregistrée. Sa validité et son quota ne sont pas encore vérifiés.', 'Enregistrer la clé');
    put(gemini, gf, node('p', 'Une clé enregistrée ne prouve pas l’accès au service. Aucun appel IA de test n’est envoyé par cette page.', { class: 'small muted' }));
    const google = card('Google, séparément'); google.classList.add('wide');
    put(google, node('p', `État communiqué : ${state ? googleStatus(state.google.state) : 'non disponible'}.`), node('p', 'Une autorisation manuelle via OAuth Playground avec vos propres identifiants est possible. L’adaptateur Google et la saisie sécurisée de cette autorisation ne sont pas encore implémentés ; la connexion et la synchronisation restent indisponibles ici.', { class: 'muted' }));
    put(grid, wifi, gemini, google); content.append(grid);
  }
  function secretForm(form, path, body, secrets, message, label) {
    const submit = node('button', label, { type: 'submit', class: 'primary' }); form.append(submit);
    form.addEventListener('submit', async event => {
      event.preventDefault(); if (busy) return;
      const ownGeneration = beginMutation(); submit.disabled = true;
      // Clear the field before awaiting network; never retain a secret in application state.
      const payload = body(); secrets.forEach(input => { input.value = ''; });
      let operation;
      try { operation = api(path, { method: 'POST', body: payload }); }
      finally { for (const field of Object.keys(payload)) payload[field] = ''; }
      try { await operation; if (ownGeneration === generation && csrf) feedback(message); }
      catch (error) { if (ownGeneration === generation && csrf) feedback(errorText(error), true); }
      finally { if (finishMutation(ownGeneration)) { submit.disabled = false; await refresh(); } }
    });
  }
  function help(content) {
    const grid = node('div', null, { class: 'grid' });
    const controls = card('Quelques gestes, l’essentiel');
    put(controls, node('p', 'Repère : port USB-C vers le bas. Commandes cibles, à confirmer sur le firmware matériel.', { class: 'muted small' }));
    const list = node('ul', null, { class: 'help-list' });
    for (const text of ['À gauche, haut / bas : parcourir les éléments.', 'Clic central gauche : valider, ou accepter l’accès web.', 'Bouton supérieur droit maintenu : enregistrer. Relâcher : arrêter et classer après sauvegarde.', 'Bouton inférieur droit, court : revenir, ou refuser l’accès web.', 'Bouton inférieur droit, long (2–3 s) : proposer le choix de veille.']) list.append(node('li', text));
    controls.append(list);
    const sleep = card('Deux façons de faire une pause');
    put(sleep, node('h3', 'Veille légère'), node('p', 'Le contenu reste à l’écran. Le compagnon peut rester accessible après autorisation. Par défaut : après 30 secondes d’inactivité.'), node('h3', 'Veille profonde'), node('p', 'Le post-it reste affiché, sinon l’écran affiche Mon compagnon. Réseau coupé, aucun rappel ni réveil programmé. Réveil physique uniquement. Par défaut : après 10 minutes depuis la dernière interaction.'), node('p', 'Ces délais décrivent la politique cible ; aucun réglage de veille à distance n’est proposé par cette API.', { class: 'small muted' }));
    const limits = card('Cette première étape'); limits.classList.add('wide');
    put(limits, node('p', 'Disponibles avec un serveur compatible : accès autorisé, consultation, notes et post-it, lecture audio, saisie Wi-Fi et clé Gemini.'), node('p', 'Import PDF/EPUB, lecture de livres, météo, capteurs, rappels, réglages matériels et actions Google ne sont pas encore proposés ici. Une interface host de démonstration n’est pas un firmware validé.', { class: 'muted' }), node('p', 'Une session expire après 10 minutes d’inactivité et peut être révoquée sur l’appareil. Les vérifications automatiques de cette page ne prolongent pas la session.', { class: 'small' }));
    put(grid, controls, sleep, limits); content.append(grid);
  }
  window.addEventListener('pagehide', () => { cleanup(); csrf = null; state = null; editing = null; app.replaceChildren(); });
  window.addEventListener('pageshow', event => { if (event.persisted) { locked(); checkSession(); } });
  locked(); checkSession();
})();
