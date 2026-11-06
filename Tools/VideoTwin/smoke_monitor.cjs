const { chromium } = require('C:/Users/ADMIN/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const assert = require('node:assert/strict');
const out = 'D:/0bay/OfficeShell/Saved/MonitorGateway/smoke';
async function testCef() {
  const targets = await (await fetch(`http://127.0.0.1:${process.env.MONITOR_TEST_CEF}/json`)).json();
  assert.ok(targets.length, 'UE must have an open web business page');
  const ws = new WebSocket(targets[0].webSocketDebuggerUrl);
  await new Promise(resolve => ws.addEventListener('open', resolve));
  let sequence = 0;
  const pending = new Map();
  ws.addEventListener('message', event => {
    const data = JSON.parse(event.data);
    if (pending.has(data.id)) { pending.get(data.id)(data); pending.delete(data.id); }
  });
  const call = (method, params) => new Promise((resolve, reject) => {
    const id = ++sequence;
    const timer = setTimeout(() => { pending.delete(id); reject(Error('CEF timeout: ' + method)); }, 15000);
    pending.set(id, data => { clearTimeout(timer); data.error ? reject(Error(JSON.stringify(data.error))) : resolve(data.result); });
    ws.send(JSON.stringify({ id, method, params }));
  });
  const evaluate = async expression => (await call('Runtime.evaluate', { expression, returnByValue: true })).result?.value;
  const waitLive = async n => {
    for (let i = 0; i < 120; ++i) {
      if (await evaluate(`document.querySelectorAll('.live-tile[data-state="live"]').length`) === n) return;
      await new Promise(resolve => setTimeout(resolve, 500));
    }
    throw Error('CEF did not decode ' + n + ' live streams: ' + await evaluate('document.body.innerText'));
  };
  try {
    await call('Page.navigate', { url: 'http://127.0.0.1:5018/monitor-test.html?camera_id=17' });
    await waitLive(1);
    assert.equal(await evaluate(`MediaSource.isTypeSupported('video/mp4; codecs="avc1.42E01E"')`), false);
    console.log('PASS actual UE CEF VP9 main-stream decode');
    await evaluate(`document.getElementById('overview').click()`);
    await waitLive(4);
    console.log('PASS actual UE CEF four VP9 substreams');
    await evaluate(`window.OntoTwinBridge.receive({type:'visibility_changed',payload:{}})`);
    assert.equal(await evaluate(`document.querySelectorAll('video').length`), 0);
    await evaluate(`window.OntoTwinBridge.receive({type:'context',payload:{}})`);
    await waitLive(4);
    console.log('PASS UE hide and reopen releases/restarts streams');
    const shot = await call('Page.captureScreenshot', { format: 'png' });
    require('node:fs').writeFileSync(`${out}/ue-cef-vp9.png`, Buffer.from(shot.data, 'base64'));
    await evaluate(`window.OntoTwinBridge.receive({type:'visibility_changed',payload:{}})`);
  } finally { ws.close(); }
}
(async () => {
  const browser = await chromium.launch({ channel: 'chrome', headless: true });
  const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  const live = count => page.waitForFunction(n => document.querySelectorAll('.live-tile[data-state="live"]').length === n, count, { timeout: 60000 });
  try {
    await page.goto('http://127.0.0.1:5018/monitor-test.html?camera_id=17');
    await live(1);
    assert.equal(await page.locator('video').count(), 1);
    assert.equal(await page.locator('#quality').inputValue(), 'main');
    console.log('PASS main stream decoded');
    await page.selectOption('#quality', 'sub'); await live(1);
    console.log('PASS substream switch decoded');
    await page.click('#overview'); await live(4);
    await page.screenshot({ path: `${out}/overview.png` });
    console.log('PASS overview four decoded substreams');
    await page.click('#next-page'); await live(4);
    assert.equal(await page.locator('#page-label').textContent(), '2 / 4');
    await page.click('a[data-camera-id="30"]'); await live(1);
    assert.equal(await page.locator('#quality').inputValue(), 'main');
    console.log('PASS pagination and camera 30');
    await page.click('a[data-camera-id="17"]');
    await page.click('a[data-camera-id="20"]'); await live(1);
    assert.equal(await page.locator('video').count(), 1);
    await page.click('#close-viewer');
    assert.equal(await page.locator('video').count(), 0);
    console.log('PASS rapid switch and close cleanup');
    await page.goto('http://127.0.0.1:5018/monitor-test.html?camera_id=31');
    assert.equal(await page.locator('video').count(), 0);
    assert.match(await page.locator('#player-status').textContent(), /无效/);
    await page.goto('http://127.0.0.1:5018/monitor-test.html?camera_id=17'); await live(1);
    await page.request.post('http://127.0.0.1:5018/__test__/signal/stop');
    await page.waitForSelector('.live-tile[data-state="offline"]', { timeout: 60000 });
    assert.equal(await page.locator('video').count(), 0);
    await page.screenshot({ path: `${out}/offline.png` });
    console.log('PASS source loss clears stale picture');
    await page.request.post('http://127.0.0.1:5018/__test__/signal/start'); await live(1);
    await page.screenshot({ path: `${out}/recovered.png` });
    console.log('PASS automatic recovery');
    await page.setViewportSize({ width: 1280, height: 720 });
    await page.screenshot({ path: `${out}/compact.png` });
    assert.deepEqual(errors, []);
    console.log('PASS browser smoke test; no page exceptions');
    if (process.env.MONITOR_TEST_CEF) await testCef();
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
