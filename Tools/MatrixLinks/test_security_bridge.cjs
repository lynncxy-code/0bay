// Exercise the real page bridge with delayed UE bindings and a lost host handshake.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const intervals = new Map(), timeouts = new Map(), events = new Map(), sent = [];
let id = 0;
const document = {
  readyState: 'complete', documentElement: {}, body: { dataset: {} },
  addEventListener: (name, fn) => events.set(name, fn), querySelectorAll: () => []
};
const window = {
  setInterval(fn) { intervals.set(++id, fn); return id; },
  clearInterval(key) { intervals.delete(key); },
  setTimeout(fn) { timeouts.set(++id, fn); return id; },
  clearTimeout(key) { timeouts.delete(key); },
  addEventListener() {}, dispatchEvent() {}
};
vm.runInNewContext(fs.readFileSync(process.argv[2] || 'D:/0bay/NetworkSecurity/ue-bridge.js', 'utf8'), {
  window, document, CustomEvent: class {}, MutationObserver: class { observe() {} }
});
assert.equal(intervals.size, 1, 'retry while the native object is absent');
window.ue = { ontotwinwebbridge: { onmessage(raw) {
  const m = JSON.parse(raw); sent.push(m);
  if (m.type === 'ready') window.OntoTwinBridge.receive({type:'host_ready', payload:{compatible:true}});
} } };
for (const fn of [...intervals.values()]) fn();
assert.equal(intervals.size, 0, 'stop retries after acknowledgement');
for (const fn of [...timeouts.values()]) fn();
assert.equal(sent.filter(m => m.type === 'interactive_regions').length, 1);
window.OntoTwinBridge.receive({type:'action_result', payload:{error_code:'bridge_not_ready'}});
assert.equal(sent.filter(m => m.type === 'ready').length, 2, 'recover when host loses state');
for (const fn of [...timeouts.values()]) fn();
assert.equal(sent.filter(m => m.type === 'interactive_regions').length, 2, 're-send unchanged regions on reconnect');
assert.equal(intervals.size, 0);
assert.equal(typeof events.get('ue:ready'), 'function');
console.log('PASS: delayed binding, acknowledged retry, reconnect, region replay');
