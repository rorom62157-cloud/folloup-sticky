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
    refresh, api, beginMutation,
    state: () => state,
    invalidateSession: () => cleanup()
  };`);
function deferred() {
  let resolve, reject;
  const promise = new Promise((ok, fail) => { resolve = ok; reject = fail; });
  return {promise, resolve, reject};
}
function fixture() {
  const requests = [];
  const app = {querySelectorAll: () => [], replaceChildren() {}};
  const context = vm.createContext({
    document: {getElementById: id => id === 'app' ? app : null},
    window: {addEventListener() {}},
    AbortController, DOMException: TestDOMException, URL,
    setTimeout: () => 1, clearTimeout() {},
    fetch: () => { const d = deferred(); requests.push(d); return d.promise; }
  });
  new vm.Script(instrumented, {filename: 'web/app.js'}).runInContext(context);
  return {client: context.client, requests};
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
console.log('web_races: all assertions passed (transport fixture, not browser validation)');
