// Executes actual client functions with a minimal transport fixture, not a browser/DOM test.
import fs from 'node:fs/promises';
import vm from 'node:vm';
import assert from 'node:assert/strict';
class TestDOMException extends Error { constructor(message, name) { super(message); this.name = name; } }
const source = await fs.readFile(new URL('../web/app.js', import.meta.url), 'utf8');
const entry = '  locked(); checkSession();';
assert.equal(source.split(entry).length, 2, 'Client bootstrap changed: update test seam deliberately');
const instrumented = source.replace(entry, `
  csrf = 'test-csrf'; view = 'settings';
  globalThis.client = {
    refresh, api, beginMutation, requestAccess, checkSession, locked, editNote,
    state: () => state,
    busy: () => busy,
    editing: () => editing,
    invalidateSession: () => cleanup()
  };`);
function deferred() {
  let resolve, reject;
  const promise = new Promise((ok, fail) => { resolve = ok; reject = fail; });
  return {promise, resolve, reject};
}
function fixture() {
  const requests = [];
  const timers = new Map(); let timerId = 0;
  class Element {
    constructor(tag) { this.tag = tag; this.children = []; this.attrs = {}; this.handlers = {}; this.dataset = {}; this.classList = {add() {}}; }
    append(...children) { this.children.push(...children); }
    replaceChildren(...children) { this.children = children; }
    setAttribute(key, value) { this.attrs[key] = value; }
    addEventListener(key, handler) { this.handlers[key] = handler; }
    querySelectorAll() { return []; }
    scrollIntoView() {}
    focus() {}
  }
  const app = new Element('main');
  const find = predicate => {
    const visit = el => predicate(el) ? el : el.children.map(visit).find(Boolean);
    return visit(app);
  };
  app.append(Object.assign(new Element('div'), {attrs:{id:'editor'}}), Object.assign(new Element('p'), {attrs:{id:'feedback'}}));
  const context = vm.createContext({
    document: {getElementById: id => id === 'app' ? app : find(el => el.attrs.id === id), createElement: tag => new Element(tag), querySelector: selector => selector === '.lock' ? find(el => el.attrs.class === 'lock') : null},
    window: {addEventListener() {}},
    AbortController, DOMException: TestDOMException, URL, TextEncoder,
    setTimeout: (fn, delay) => { const id = ++timerId; timers.set(id, {fn, delay}); return id; }, clearTimeout: id => timers.delete(id),
    fetch: (path, options) => { const d = deferred(); requests.push({...d, path, options}); return d.promise; }
  });
  new vm.Script(instrumented, {filename: 'web/app.js'}).runInContext(context);
  return {client: context.client, requests, timers, find};
}
const response = value => ({ok:true,status:200,json:async()=>value});
{
  const f = fixture();
  const older = f.client.refresh();
  const newer = f.client.refresh();
  f.requests[1].resolve(response({revision:2})); await newer;
  f.requests[0].resolve(response({revision:1})); await older;
  assert.equal(f.client.state().revision, 2, 'Older response must not replace newer state');
}
{
  const f = fixture();
  const beforeMutation = f.client.refresh();
  f.client.beginMutation();
  f.requests[0].resolve(response({revision:1})); await beforeMutation;
  assert.equal(f.client.state(), null, 'Mutation invalidates already in-flight refresh');
}
{
  const f = fixture(), body = deferred(), parsing = deferred();
  const request = f.client.api('/api/state');
  // Attach rejection handler immediately; deliberately emulate a body that ignores abort.
  const outcome = request.then(()=>({ok:true}),error=>({ok:false,name:error.name}));
  f.requests[0].resolve({ok:true,status:200,json:()=>{parsing.resolve();return body.promise;}});
  await parsing.promise;
  f.client.invalidateSession();
  body.resolve({private:'old session'});
  const result = await outcome;
  assert.equal(result.ok, false);
  assert.equal(result.name, 'AbortError', 'Session change during body parse must reject result');
}
{
  const f = fixture(); f.client.locked();
  const initialCheck = f.client.checkSession();
  const button = f.find(el => el.tag === 'button');
  const access = f.client.requestAccess({currentTarget:button});
  assert.equal(f.requests[0].options.signal.aborted, true, 'Access POST aborts bootstrap GET');
  await f.client.checkSession();
  assert.equal(f.requests.length, 2, 'No session poll starts during access POST');
  f.requests[0].resolve(response({state:'denied'})); await initialCheck;
  assert.equal(f.requests[1].options.signal.aborted, false, 'Stale denied GET cannot abort access POST');
  f.requests[1].resolve(response({state:'pending'})); await access;
  assert.equal(f.client.busy(), false);
  assert.match(f.find(el => el.attrs.id === 'lock-title').textContent, /Un clic/);
  const poll = [...f.timers.values()].find(timer => timer.delay === 3000);
  assert.ok(poll, 'Pending access resumes session polling');
  const resumedCheck = poll.fn();
  assert.equal(f.requests[2].path, '/api/session');
  f.requests[2].resolve(response({state:'pending'})); await resumedCheck;
}
{
  const f = fixture(); f.client.locked();
  const button = f.find(el => el.tag === 'button');
  const access = f.client.requestAccess({currentTarget:button});
  f.requests[0].reject(new Error('Réseau indisponible')); await access;
  assert.equal(f.client.busy(), false, 'Failed access clears busy');
  assert.equal(button.disabled, false, 'Failed access permits retry');
  assert.ok([...f.timers.values()].some(timer => timer.delay === 3000), 'Failed access resumes checking');
}
{
  const f = fixture();
  const exact = {text:'a'.repeat(24565)}; // JSON syntax adds 11 bytes.
  const accepted = f.client.api('/api/notes', {method:'POST',body:exact});
  assert.equal(new TextEncoder().encode(f.requests[0].options.body).byteLength, 24576);
  f.requests[0].resolve(response({})); await accepted;
  for (const body of [{text:'a'.repeat(24566)}, {text:'é'.repeat(12283)}, {text:'\n'.repeat(12283)}]) {
    await assert.rejects(f.client.api('/api/notes', {method:'POST',body}), error => error.status === 413 && /rien n’a été enregistré/.test(error.message));
  }
  assert.equal(f.requests.length, 1, 'Oversized encoded JSON is rejected before fetch');
}
{
  const f = fixture(); f.client.editNote(null);
  const title = f.find(el => el.attrs.id === 'note-title');
  const text = f.find(el => el.attrs.id === 'note-text');
  const form = f.find(el => el.tag === 'form');
  assert.equal(title.maxLength, 160); assert.equal(text.maxLength, 4096);
  for (const [titleValue, textValue] of [['é'.repeat(81), 'note'], ['titre', '🙂'.repeat(1025)]]) {
    title.value = titleValue; text.value = textValue;
    await form.handlers.submit({preventDefault() {}});
    assert.equal(f.requests.length, 0, 'NoteStore UTF-8 byte caps checked before transport');
    assert.equal(f.client.busy(), false);
    assert.ok(f.client.editing(), 'Oversized draft stays open');
    assert.equal(text.value, textValue, 'Oversized text remains available to edit');
    assert.match(f.find(el => el.attrs.id === 'feedback').textContent, /rien n’a été enregistré/);
  }
  title.value = 'é'.repeat(80); text.value = '🙂'.repeat(1024);
  const save = form.handlers.submit({preventDefault() {}});
  assert.equal(f.requests.length, 1, 'Exact NoteStore byte boundaries reach transport');
  f.requests[0].resolve({ok:false,status:503,json:async()=>({message:'Stockage indisponible'})});
  // Saving finishes with one refresh, whose response must also be drained.
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(f.requests[1].path, '/api/state');
  f.requests[1].resolve(response({})); await save;
  assert.equal(f.client.busy(), false);
  assert.ok(f.client.editing(), 'Failed save leaves draft open');
  assert.match(f.find(el => el.attrs.id === 'feedback').textContent, /Stockage indisponible/);
}
console.log('web_races: all assertions passed (transport fixture, not browser validation)');
