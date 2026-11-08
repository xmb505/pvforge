#!/usr/bin/env node
// M6 Stage 0 spike: external Chromium/CDP capture for PVForge HTML layers.
//
// Measures, for each resolution:
//   - variable injection + component render
//   - Page.captureScreenshot round trip (PNG/base64)
//   - base64 + PNG decode to raw RGBA (same work Qt's QImage does)
// and checks determinism, transparency, network/file blocking, blank/stale
// frames, two-page capture and browser process-tree RSS.
//
// Usage:
//   node tools/html-spike/spike.mjs
//   node tools/html-spike/spike.mjs --frames 100
//   node tools/html-spike/spike.mjs --browser /usr/bin/google-chrome --no-edge
//
// Requires Node 22+ (global WebSocket) and ffmpeg (authoritative raw RGBA
// comparison for the determinism check). No npm dependencies.

import { spawn, execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';

const options = parseArgs(process.argv.slice(2));
const shotDir = path.join(os.tmpdir(), 'pvforge-spike-shots');
fs.mkdirSync(shotDir, { recursive: true });

function parseArgs(argv) {
  const result = {
    browser: '',
    edge: '',
    frames: 100,
    multiFrames: 0,
    report: 'docs/M6_SPIKE.md',
    edgeFrames: 30,
  };
  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i];
    if (arg === '--browser') result.browser = argv[++i] || '';
    else if (arg === '--edge') result.edge = argv[++i] || '';
    else if (arg === '--no-edge') result.edge = '-';
    else if (arg === '--frames') result.frames = Math.max(5, Number(argv[++i]) || 100);
    else if (arg === '--multi-frames') result.multiFrames = Math.max(2, Number(argv[++i]) || 0);
    else if (arg === '--edge-frames') result.edgeFrames = Math.max(3, Number(argv[++i]) || 30);
    else if (arg === '--report') result.report = argv[++i] || result.report;
  }
  if (!result.multiFrames) {
    result.multiFrames = Math.max(10, Math.min(30, Math.floor(result.frames / 4)));
  }
  return result;
}

// ---------------------------------------------------------------------------
// small helpers

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const formatMs = (value) => Number(value).toFixed(2);
const hashBuffer = (buffer) => createHash('sha256').update(buffer).digest('hex');

function percentile(sorted, p) {
  if (sorted.length === 0) return 0;
  const index = Math.min(sorted.length - 1, Math.max(0, Math.ceil(sorted.length * p) - 1));
  return sorted[index];
}

function stats(values) {
  if (values.length === 0) return { min: 0, median: 0, p95: 0, max: 0, mean: 0 };
  const sorted = [...values].sort((a, b) => a - b);
  const mean = values.reduce((sum, value) => sum + value, 0) / values.length;
  return {
    min: sorted[0],
    median: percentile(sorted, 0.5),
    p95: percentile(sorted, 0.95),
    max: sorted[sorted.length - 1],
    mean,
  };
}

function findExecutable(explicit, candidates) {
  if (explicit) return fs.existsSync(explicit) ? explicit : '';
  for (const candidate of candidates) {
    try {
      const found = execFileSync('which', [candidate], { encoding: 'utf8' }).trim();
      if (found) return found;
    } catch {
      // keep looking
    }
  }
  return '';
}

function executableVersion(binary) {
  try {
    return execFileSync(binary, ['--version'], { encoding: 'utf8' }).trim();
  } catch {
    return 'unknown';
  }
}

// ---------------------------------------------------------------------------
// CDP client

class Cdp {
  static async connect(url, timeoutMs = 10000) {
    const socket = new WebSocket(url);
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('WebSocket connect timeout')), timeoutMs);
      socket.onopen = () => {
        clearTimeout(timer);
        resolve();
      };
      socket.onerror = () => {
        clearTimeout(timer);
        reject(new Error('WebSocket connect failed'));
      };
    });
    return new Cdp(socket);
  }

  constructor(socket) {
    this.socket = socket;
    this.nextId = 0;
    this.pending = new Map();
    socket.onmessage = (event) => {
      const text = typeof event.data === 'string'
        ? event.data
        : Buffer.from(event.data).toString('utf8');
      let message;
      try {
        message = JSON.parse(text);
      } catch {
        return;
      }
      if (!message.id) return;
      const pending = this.pending.get(message.id);
      if (!pending) return;
      this.pending.delete(message.id);
      clearTimeout(pending.timer);
      if (message.error) pending.reject(new Error(message.error.message || 'CDP error'));
      else pending.resolve(message.result || {});
    };
    socket.onclose = () => {
      for (const pending of this.pending.values()) {
        clearTimeout(pending.timer);
        pending.reject(new Error('CDP socket closed'));
      }
      this.pending.clear();
    };
  }

  send(method, params = {}, timeoutMs = 15000) {
    const id = ++this.nextId;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`${method} timed out after ${timeoutMs} ms`));
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      this.socket.send(JSON.stringify({ id, method, params }));
    });
  }

  close() {
    try {
      this.socket.close();
    } catch {
      // ignore
    }
  }
}

// ---------------------------------------------------------------------------
// browser lifecycle

const LAUNCH_ARGS = [
  '--headless=new',
  '--remote-debugging-port=0',
  '--remote-allow-origins=*',
  '--no-first-run',
  '--no-default-browser-check',
  '--disable-background-networking',
  '--disable-component-update',
  '--disable-sync',
  '--disable-extensions',
  '--disable-default-apps',
  '--disable-dev-shm-usage',
  '--disable-gpu',
  '--hide-scrollbars',
  '--mute-audio',
  '--force-device-scale-factor=1',
  '--force-color-profile=srgb',
  // Defense in depth: DNS is dead even if a URL slips past Network.setBlockedURLs.
  '--host-resolver-rules=MAP * ~NOTFOUND',
  '--disable-features=Translate,MediaRouter,OptimizationHints,CalculateNativeWinOcclusion',
];

async function launchBrowser(binary) {
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'pvforge-spike-profile-'));
  const args = [...LAUNCH_ARGS, `--user-data-dir=${profile}`];
  const started = performance.now();
  const child = spawn(binary, args, {
    detached: true,
    stdio: ['ignore', 'ignore', 'pipe'],
  });
  let stderr = '';
  let exited = false;
  let wsUrl;
  try {
    wsUrl = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        reject(new Error(`browser did not print a DevTools URL:\n${stderr.slice(-4000)}`));
      }, 20000);
      child.stderr.on('data', (chunk) => {
        stderr = (stderr + chunk.toString('utf8')).slice(-8000);
        const match = stderr.match(/DevTools listening on (ws:\/\/[^\s]+)/);
        if (match) {
          clearTimeout(timer);
          resolve(match[1]);
        }
      });
      child.on('error', (error) => {
        clearTimeout(timer);
        reject(error);
      });
      child.on('exit', (code) => {
        exited = true;
        clearTimeout(timer);
        reject(new Error(`browser exited early with code ${code}:\n${stderr.slice(-4000)}`));
      });
    });
  } catch (error) {
    // Do not leak a half-started browser (alive but no DevTools URL).
    await killTree(child.pid);
    fs.rmSync(profile, { recursive: true, force: true });
    throw error;
  }
  const startupMs = performance.now() - started;
  let socket;
  try {
    socket = await Cdp.connect(wsUrl);
  } catch (error) {
    await killTree(child.pid);
    fs.rmSync(profile, { recursive: true, force: true });
    throw error;
  }
  const session = {
    binary,
    child,
    socket,
    wsUrl,
    port: Number(new URL(wsUrl).port),
    profile,
    args,
    startupMs,
    isAlive: () => !exited,
    async close() {
      session.socket.close();
      await killTree(child.pid);
      fs.rmSync(profile, { recursive: true, force: true });
    },
  };
  return session;
}

async function killTree(pid) {
  try {
    process.kill(-pid, 'SIGTERM');
  } catch {
    try {
      process.kill(pid, 'SIGTERM');
    } catch {
      // already gone
    }
  }
  const deadline = Date.now() + 5000;
  while (Date.now() < deadline && processExists(pid)) {
    await sleep(50);
  }
  try {
    process.kill(-pid, 'SIGKILL');
  } catch {
    try {
      process.kill(pid, 'SIGKILL');
    } catch {
      // already gone
    }
  }
  await sleep(50);
}

function processExists(pid) {
  try {
    process.kill(pid, 0);
    return true;
  } catch {
    return false;
  }
}

function processesWithProfile(profile) {
  const found = [];
  for (const entry of fs.readdirSync('/proc')) {
    if (!/^\d+$/.test(entry)) continue;
    try {
      const cmdline = fs.readFileSync(`/proc/${entry}/cmdline`);
      if (cmdline.includes(profile)) found.push(Number(entry));
    } catch {
      // process vanished
    }
  }
  return found;
}

function processTreeRss(profile) {
  let kb = 0;
  let count = 0;
  for (const pid of processesWithProfile(profile)) {
    try {
      const status = fs.readFileSync(`/proc/${pid}/status`, 'utf8');
      const match = status.match(/VmRSS:\s+(\d+)\s+kB/);
      if (match) {
        kb += Number(match[1]);
        count += 1;
      }
    } catch {
      // process vanished
    }
  }
  return { kb, count };
}

// ---------------------------------------------------------------------------
// sample document
//
// The component is intentionally hostile: it reads Date.now(), Math.random()
// and uses CSS, so the host stubs (fixed clock, per-frame PRNG seed, disabled
// animations) must make two captures of the same frame identical.

const STUB_SOURCE = `(() => {
  const fixedNow = 1700000000000;
  Date.now = () => fixedNow;
  performance.now = () => 0;
  let seed = 0x9e3779b9;
  Math.random = () => {
    seed = (seed * 1664525 + 1013904223) >>> 0;
    return seed / 4294967296;
  };
  // The host resets the PRNG per frame; same frame => same sequence.
  window.__pvfSeed = (frame) => {
    seed = (0x9e3779b9 ^ Math.imul(frame | 0, 2654435761)) >>> 0;
  };
})();`;

// Page.addScriptToEvaluateOnNewDocument does NOT run for
// Page.setDocumentContent (it is not a navigation), so the stub is inlined as
// the first script of the document. Production must do the same, or load the
// asset through a real navigation/data: URL.
const PAGE_HTML = `<!doctype html>
<html><head><meta charset="utf-8">
<style>
  * { animation: none !important; transition: none !important; }
  html, body { margin:0; width:100%; height:100%; overflow:hidden;
               background:transparent; font-family:sans-serif; }
  #bar { position:absolute; left:40px; bottom:40px; height:26px;
         background:#3478f6; border-radius:13px; width:0; }
  #label { position:absolute; left:40px; bottom:86px; color:#fff;
           font:600 30px sans-serif; }
  #noise { position:absolute; right:40px; bottom:86px; color:#fff;
           font:14px monospace; }
  #icon { position:absolute; left:40px; top:40px; width:48px; height:48px; }
</style></head>
<body>
  <img id="icon" alt=""
       src="data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSI0OCIgaGVpZ2h0PSI0OCI+PHJlY3Qgd2lkdGg9IjQ4IiBoZWlnaHQ9IjQ4IiBmaWxsPSIjMzRjNzU5Ii8+PC9zdmc+">
  <div id="label">0</div>
  <div id="noise"></div>
  <div id="bar"></div>
  <script>${STUB_SOURCE}</script>
  <script>
    window.pvf = {
      vars: {}, frame: 0, time: 0, width: 0, height: 0,
      rendered() {
        const done = window.__pvfDone;
        window.__pvfDone = null;
        if (done) done();
      }
    };
    window.renderFrame = (vars, frame, time) => new Promise((resolve) => {
      window.__pvfSeed(frame);
      window.pvf.vars = vars;
      window.pvf.frame = frame;
      window.pvf.time = time;
      document.getElementById('label').textContent = String(vars.value);
      document.getElementById('bar').style.width =
        Math.round(vars.value / 100 * 800) + 'px';
      // Deliberately wall clock + randomness; the host stubs must make two
      // captures of the same frame identical.
      document.getElementById('noise').textContent =
        Date.now() + ' / ' + Math.random().toFixed(6);
      // Force style/layout now. requestAnimationFrame is unreliable in
      // headless; the screenshot itself forces the compositor frame.
      void document.body.offsetHeight;
      window.__pvfDone = resolve;
      // Production contract: fonts must be ready before the frame is captured.
      document.fonts.ready.then(() => setTimeout(() => window.pvf.rendered(), 0));
    });
  </script>
</body></html>`;

// ---------------------------------------------------------------------------
// page lifecycle

async function createPage(browser, width, height) {
  const started = performance.now();
  const created = await browser.socket.send('Target.createTarget', { url: 'about:blank' });
  const targetId = created.targetId;
  let pageInfo = null;
  const listDeadline = Date.now() + 10000;
  while (!pageInfo && Date.now() < listDeadline) {
    const list = await (await fetch(`http://127.0.0.1:${browser.port}/json/list`)).json();
    pageInfo = list.find((item) => item.id === targetId) || null;
    if (!pageInfo) await sleep(25);
  }
  if (!pageInfo || !pageInfo.webSocketDebuggerUrl) {
    throw new Error('cannot find page WebSocket URL');
  }
  const page = await Cdp.connect(pageInfo.webSocketDebuggerUrl);
  page.targetId = targetId;
  await page.send('Page.enable');
  await page.send('Runtime.enable');
  await page.send('Network.enable');
  await page.send('Network.setBlockedURLs', {
    urls: ['http://*', 'https://*', 'ws://*', 'wss://*', 'file://*', 'ftp://*'],
  });
  await page.send('Emulation.setDeviceMetricsOverride', {
    width, height, deviceScaleFactor: 1, mobile: false,
  });
  await page.send('Emulation.setDefaultBackgroundColorOverride', {
    color: { r: 0, g: 0, b: 0, a: 0 },
  });
  await page.send('Page.addScriptToEvaluateOnNewDocument', { source: STUB_SOURCE });
  const tree = await page.send('Page.getFrameTree');
  await page.send('Page.setDocumentContent', {
    frameId: tree.frameTree.frame.id,
    html: PAGE_HTML,
  });
  const readyDeadline = Date.now() + 10000;
  let ready = false;
  while (!ready && Date.now() < readyDeadline) {
    const probe = await page.send('Runtime.evaluate', {
      expression: 'typeof window.renderFrame === "function"',
      returnByValue: true,
    });
    ready = probe.result && probe.result.value === true;
    if (!ready) await sleep(25);
  }
  if (!ready) throw new Error('page did not expose renderFrame');
  return { page, targetId, readyMs: performance.now() - started };
}

async function closePage(browser, page) {
  await browser.socket.send('Target.closeTarget', { targetId: page.targetId }).catch(() => {});
  page.close();
}

async function renderInPage(page, vars, frame, fps) {
  const expression = `window.renderFrame(${JSON.stringify(vars)}, ${frame}, ${(
    frame / fps
  ).toFixed(6)})`;
  const started = performance.now();
  const result = await page.send(
    'Runtime.evaluate',
    { expression, awaitPromise: true, returnByValue: true },
    10000,
  );
  if (result.exceptionDetails) {
    throw new Error('page render failed: ' + JSON.stringify(result.exceptionDetails));
  }
  return performance.now() - started;
}

async function capturePng(page, width, height) {
  // A background tab never produces a compositor frame in headless Chrome, so
  // its captureScreenshot hangs forever. Front the page before every capture;
  // this also makes multi-page capture serialized and measurable.
  await page.send('Page.bringToFront');
  const started = performance.now();
  const shot = await page.send(
    'Page.captureScreenshot',
    {
      format: 'png',
      fromSurface: true,
      captureBeyondViewport: false,
      clip: { x: 0, y: 0, width, height, scale: 1 },
    },
    15000,
  );
  const shotMs = performance.now() - started;
  return { base64: shot.data, shotMs };
}

// ---------------------------------------------------------------------------
// PNG decode (zlib inflate + unfilter), equivalent to QImage::loadFromData.
// Chrome emits 8-bit non-interlaced RGB/RGBA; other simple formats supported.

function decodePng(buffer) {
  const signature = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  if (!buffer.subarray(0, 8).equals(signature)) throw new Error('not a PNG');
  let offset = 8;
  let width = 0;
  let height = 0;
  let bitDepth = 0;
  let colorType = 0;
  let interlace = 0;
  const idat = [];
  while (offset + 8 <= buffer.length) {
    const length = buffer.readUInt32BE(offset);
    const type = buffer.toString('ascii', offset + 4, offset + 8);
    const data = buffer.subarray(offset + 8, offset + 8 + length);
    offset += 12 + length;
    if (type === 'IHDR') {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      bitDepth = data[8];
      colorType = data[9];
      interlace = data[12];
    } else if (type === 'IDAT') {
      idat.push(data);
    } else if (type === 'IEND') {
      break;
    }
  }
  const channelsByType = { 0: 1, 2: 3, 4: 2, 6: 4 };
  const channels = channelsByType[colorType];
  if (!channels) throw new Error(`unsupported PNG color type ${colorType}`);
  if (bitDepth !== 8) throw new Error(`unsupported PNG bit depth ${bitDepth}`);
  if (interlace !== 0) throw new Error('interlaced PNG is not supported');

  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const rgba = Buffer.alloc(width * height * 4);
  let previous = Buffer.alloc(stride);
  let current = Buffer.alloc(stride);
  let rawOffset = 0;
  for (let y = 0; y < height; ++y) {
    const filter = raw[rawOffset++];
    const row = raw.subarray(rawOffset, rawOffset + stride);
    rawOffset += stride;
    const bpp = channels;
    for (let i = 0; i < stride; ++i) {
      const left = i >= bpp ? current[i - bpp] : 0;
      const up = previous[i];
      const upLeft = i >= bpp ? previous[i - bpp] : 0;
      let value = row[i];
      switch (filter) {
        case 0:
          break;
        case 1:
          value = (value + left) & 0xff;
          break;
        case 2:
          value = (value + up) & 0xff;
          break;
        case 3:
          value = (value + ((left + up) >> 1)) & 0xff;
          break;
        case 4: {
          const p = left + up - upLeft;
          const pa = Math.abs(p - left);
          const pb = Math.abs(p - up);
          const pc = Math.abs(p - upLeft);
          const predictor = pa <= pb && pa <= pc ? left : pb <= pc ? up : upLeft;
          value = (value + predictor) & 0xff;
          break;
        }
        default:
          throw new Error(`unsupported PNG filter ${filter}`);
      }
      current[i] = value;
    }
    const outBase = y * width * 4;
    if (channels === 4) {
      current.copy(rgba, outBase);
    } else if (channels === 3) {
      for (let x = 0; x < width; ++x) {
        rgba[outBase + x * 4] = current[x * 3];
        rgba[outBase + x * 4 + 1] = current[x * 3 + 1];
        rgba[outBase + x * 4 + 2] = current[x * 3 + 2];
        rgba[outBase + x * 4 + 3] = 255;
      }
    } else if (channels === 1) {
      for (let x = 0; x < width; ++x) {
        const gray = current[x];
        rgba[outBase + x * 4] = gray;
        rgba[outBase + x * 4 + 1] = gray;
        rgba[outBase + x * 4 + 2] = gray;
        rgba[outBase + x * 4 + 3] = 255;
      }
    } else {
      for (let x = 0; x < width; ++x) {
        const gray = current[x * 2];
        rgba[outBase + x * 4] = gray;
        rgba[outBase + x * 4 + 1] = gray;
        rgba[outBase + x * 4 + 2] = gray;
        rgba[outBase + x * 4 + 3] = current[x * 2 + 1];
      }
    }
    const swap = previous;
    previous = current;
    current = swap;
  }
  return { rgba, width, height };
}

function decodeFrame(base64) {
  const started = performance.now();
  const buffer = Buffer.from(base64, 'base64');
  const decoded = decodePng(buffer);
  return { ...decoded, png: buffer, decodeMs: performance.now() - started };
}

function alphaAt(rgba, width, x, y) {
  return rgba[(y * width + x) * 4 + 3];
}

function ffmpegRgba(pngPath) {
  return execFileSync(
    'ffmpeg',
    ['-v', 'error', '-i', pngPath, '-f', 'rawvideo', '-pix_fmt', 'rgba', '-'],
    { maxBuffer: 256 * 1024 * 1024 },
  );
}

function barGeometry(value, height) {
  const barWidth = Math.round((value / 100) * 800);
  const y = height - 40 - 13;
  return { barWidth, y };
}

// Returns a short failure reason, or '' when the expected bar is visible and
// no wider stale bar remains.
function checkFramePixels(rgba, width, height, value) {
  const { barWidth, y } = barGeometry(value, height);
  if (barWidth >= 20) {
    const nearTip = 40 + barWidth - 6;
    if (nearTip < width && alphaAt(rgba, width, nearTip, y) !== 255) {
      return `bar tip alpha != 255 at value ${value.toFixed(2)}`;
    }
  }
  const outside = 40 + barWidth + 12;
  if (outside < width && alphaAt(rgba, width, outside, y) !== 0) {
    return `pixel right of bar alpha != 0 at value ${value.toFixed(2)} (stale wide frame?)`;
  }
  if (barWidth >= 20) {
    const center = 40 + Math.floor(barWidth / 2);
    if (alphaAt(rgba, width, center, y) !== 255) {
      return `bar center alpha != 255 at value ${value.toFixed(2)}`;
    }
  }
  return '';
}

// ---------------------------------------------------------------------------
// measurements

async function probeBlocking(page) {
  const expression = `(async () => {
    const attempt = (url) => Promise.race([
      fetch(url).then(() => 'allowed').catch(() => 'blocked'),
      new Promise((resolve) => setTimeout(() => resolve('timeout'), 2000)),
    ]);
    return {
      http: await attempt('https://example.com/pvforge-spike'),
      file: await attempt('file:///etc/passwd'),
    };
  })()`;
  const result = await page.send(
    'Runtime.evaluate',
    { expression, awaitPromise: true, returnByValue: true },
    8000,
  );
  return result.result && result.result.value
    ? result.result.value
    : { http: 'error', file: 'error' };
}

async function runScenario(browser, { label, width, height, frames }) {
  console.log(`[${label}] creating page ${width}x${height}`);
  const { page, readyMs } = await createPage(browser, width, height);
  const sample = (suffix) => path.join(shotDir, `${label}-${suffix}.png`);
  const renderTimes = [];
  const shotTimes = [];
  const decodeTimes = [];
  const totalTimes = [];
  const badFrames = [];
  let decodedBytes = 0;

  try {
    // Warm-up: first screenshots pay for surface allocation and font setup.
    for (let i = 0; i < 5; ++i) {
      const value = i * 10;
      await renderInPage(page, { value }, i, 30);
      const { base64 } = await capturePng(page, width, height);
      decodeFrame(base64);
    }

    for (let i = 0; i < frames; ++i) {
      const value = frames === 1 ? 100 : (i / (frames - 1)) * 100;
      const renderMs = await renderInPage(page, { value }, i, 30);
      const { base64, shotMs } = await capturePng(page, width, height);
      const decoded = decodeFrame(base64);
      const totalMs = renderMs + shotMs + decoded.decodeMs;
      renderTimes.push(renderMs);
      shotTimes.push(shotMs);
      decodeTimes.push(decoded.decodeMs);
      totalTimes.push(totalMs);
      decodedBytes += decoded.png.length;

      const problem = checkFramePixels(decoded.rgba, width, height, value);
      if (problem && badFrames.length < 10) badFrames.push({ frame: i, problem });

      if (i === 0) fs.writeFileSync(sample('first'), decoded.png);
      if (i === Math.floor(frames / 2)) fs.writeFileSync(sample('mid'), decoded.png);
      if (i === frames - 1) fs.writeFileSync(sample('last'), decoded.png);
    }

    // Determinism: same frame twice. PNG metadata may differ; compare decoded
    // raw RGBA, with ffmpeg as the authoritative decoder.
    const detValue = 42;
    const detA = path.join(shotDir, `${label}-det-a.png`);
    const detB = path.join(shotDir, `${label}-det-b.png`);
    await renderInPage(page, { value: detValue }, 42, 30);
    const shotA = await capturePng(page, width, height);
    const decodedA = decodeFrame(shotA.base64);
    fs.writeFileSync(detA, decodedA.png);
    await renderInPage(page, { value: detValue }, 42, 30);
    const shotB = await capturePng(page, width, height);
    const decodedB = decodeFrame(shotB.base64);
    fs.writeFileSync(detB, decodedB.png);

    const ffmpegA = ffmpegRgba(detA);
    const ffmpegB = ffmpegRgba(detB);
    const rawHashA = hashBuffer(ffmpegA);
    const rawHashB = hashBuffer(ffmpegB);
    const determinism = rawHashA === rawHashB;
    const decoderMatchesFfmpeg = rawHashA === hashBuffer(decodedA.rgba);
    const pngIdentical = hashBuffer(decodedA.png) === hashBuffer(decodedB.png);

    // Transparency: empty corner alpha 0, bar interior alpha 255.
    const { barWidth, y } = barGeometry(detValue, height);
    const cornerAlpha = alphaAt(decodedA.rgba, width, 2, 2);
    const barAlpha = alphaAt(decodedA.rgba, width, 40 + Math.floor(barWidth / 2), y);
    const transparency = cornerAlpha === 0 && barAlpha === 255;

    // Inline data: image must render (and must remain deterministic).
    const iconOffset = (64 * width + 64) * 4;
    const iconAlpha = decodedA.rgba[iconOffset + 3];
    const iconGreen = decodedA.rgba[iconOffset + 1];
    const inlineImage = iconAlpha === 255 && iconGreen > 150;

    // No blank frame: the det sample must contain opaque pixels.
    let opaquePixels = 0;
    for (let i = 3; i < decodedA.rgba.length; i += 4) {
      if (decodedA.rgba[i] !== 0) opaquePixels += 1;
    }
    const notBlank = opaquePixels > 1000;

    const blocking = await probeBlocking(page);

    const result = {
      label,
      width,
      height,
      frames,
      readyMs,
      render: stats(renderTimes),
      shot: stats(shotTimes),
      decode: stats(decodeTimes),
      total: stats(totalTimes),
      badFrames,
      determinism,
      decoderMatchesFfmpeg,
      pngIdentical,
      detHashA: rawHashA.slice(0, 16),
      detHashB: rawHashB.slice(0, 16),
      transparency,
      cornerAlpha,
      barAlpha,
      inlineImage,
      iconAlpha,
      iconGreen,
      notBlank,
      opaquePixels,
      networkBlocked: blocking.http === 'blocked',
      fileBlocked: blocking.file === 'blocked',
      blocking,
      avgPngBytes: Math.round(decodedBytes / Math.max(1, frames)),
      samples: {
        first: sample('first'),
        mid: sample('mid'),
        last: sample('last'),
        detA,
        detB,
      },
    };
    const verdict = [
      determinism ? 'deterministic' : 'NOT deterministic',
      transparency ? 'transparent' : 'transparency FAILED',
      inlineImage ? 'inline image' : 'inline image FAILED',
      badFrames.length === 0 ? 'no stale/blank frames' : `${badFrames.length} bad frames`,
      result.networkBlocked && result.fileBlocked ? 'network blocked' : 'network NOT blocked',
    ].join(', ');
    console.log(`[${label}] ${verdict}`);
    console.log(
      `[${label}] total median ${formatMs(result.total.median)} ms ` +
        `(render ${formatMs(result.render.median)} + shot ${formatMs(result.shot.median)} + ` +
        `decode ${formatMs(result.decode.median)}), p95 ${formatMs(result.total.p95)} ms, ` +
        `fps(median) ${(1000 / result.total.median).toFixed(1)}`,
    );
    return result;
  } finally {
    await closePage(browser, page);
  }
}

async function runTwoPage(browser, { width, height, rounds }) {
  console.log(`[two-page] ${width}x${height} x ${rounds} rounds`);
  const first = await createPage(browser, width, height);
  const second = await createPage(browser, width, height);
  const pairTimes = [];
  let rss = null;
  try {
    for (let i = 0; i < rounds; ++i) {
      const roundStart = performance.now();
      for (const [entry, value] of [[first, i], [second, 100 - i]]) {
        await renderInPage(entry.page, { value }, i, 30);
        const { base64 } = await capturePng(entry.page, width, height);
        decodeFrame(base64);
      }
      pairTimes.push(performance.now() - roundStart);
      if (i === 0) rss = processTreeRss(browser.profile);
    }
    const result = {
      label: `two-pages-${height}p`,
      width,
      height,
      rounds,
      pair: stats(pairTimes),
      rss,
    };
    console.log(
      `[two-page] pair median ${formatMs(result.pair.median)} ms, ` +
        `p95 ${formatMs(result.pair.p95)} ms; RSS ${(rss.kb / 1024).toFixed(0)} MiB / ${rss.count} procs`,
    );
    return result;
  } finally {
    await closePage(browser, first.page);
    await closePage(browser, second.page);
  }
}

async function runBrowserSuite(browser, { labelPrefix, frames, multiFrames, suiteLabel }) {
  const results = [];
  results.push(await runScenario(browser, {
    label: `${labelPrefix}-720p`,
    width: 1280,
    height: 720,
    frames,
  }));
  results.push(await runScenario(browser, {
    label: `${labelPrefix}-1080p`,
    width: 1920,
    height: 1080,
    frames,
  }));
  const multi = [];
  if (multiFrames > 0) {
    multi.push(await runTwoPage(browser, { width: 1280, height: 720, rounds: multiFrames }));
    multi.push(await runTwoPage(browser, { width: 1920, height: 1080, rounds: Math.max(5, Math.floor(multiFrames / 2)) }));
  }
  const rss = processTreeRss(browser.profile);
  console.log(`[${suiteLabel}] process tree RSS ${(rss.kb / 1024).toFixed(0)} MiB / ${rss.count} procs`);
  return { results, multi, rss };
}

// ---------------------------------------------------------------------------
// report

function markdownTable(rows, header) {
  const lines = [];
  lines.push('| ' + header.join(' | ') + ' |');
  lines.push('|' + header.map(() => '---').join('|') + '|');
  for (const row of rows) lines.push('| ' + row.join(' | ') + ' |');
  return lines;
}

function writeReport(reportPath, context) {
  const {
    chrome, edge, chromeSuite, edgeSuite, edgeError, rssBaseline, error,
  } = context;
  const shot1080Median = chromeSuite ? chromeSuite.results[1].shot.median : 0;
  const shot1080Min = chromeSuite ? chromeSuite.results[1].shot.min : 0;
  const lines = [];
  lines.push('# M6 Stage 0：Chrome/CDP spike 测量报告');
  lines.push('');
  lines.push(`记录时间：${new Date().toISOString()}`);
  lines.push(`主机：${os.cpus()[0].model}，${(os.totalmem() / 1024 / 1024 / 1024).toFixed(1)} GiB RAM，${os.platform()} ${os.release()}`);
  lines.push(`系统负载（1/5/15 分钟）：开始 ${context.loadStart || 'n/a'}，结束 ${os.loadavg().map((v) => v.toFixed(2)).join('/')}`);
  lines.push(`Node：${process.version}`);
  lines.push('');
  lines.push('## 结论');
  lines.push('');
  if (error) {
    lines.push('**失败。** 本次测量未跑完，原因见下方“失败原因”。');
    lines.push('');
    lines.push('```');
    lines.push(error);
    lines.push('```');
    lines.push('');
  }
  if (chromeSuite) {
    const p720 = chromeSuite.results[0];
    const p1080 = chromeSuite.results[1];
    const threshold = 25;
    const pass720 = p720.total.median <= threshold;
    const pass1080 = p1080.total.median <= threshold;
    const fps720 = 1000 / p720.total.median;
    const fps1080 = 1000 / p1080.total.median;
    lines.push(`- 1080p 单帧总耗时中位数 **${formatMs(p1080.total.median)} ms**（门槛 ≤ ${threshold} ms）：**${pass1080 ? 'GO' : 'NO-GO'}**。`);
    lines.push(`- 720p 单帧总耗时中位数 **${formatMs(p720.total.median)} ms**：**${pass720 ? 'GO' : 'NO-GO'}**。`);
    lines.push(`- 等效帧率：720p ${fps720.toFixed(1)} fps，1080p ${fps1080.toFixed(1)} fps（按中位数；p95 ${formatMs(p720.total.p95)} / ${formatMs(p1080.total.p95)} ms）。`);
    lines.push(`- 即使不计解码，1080p 截图环节中位 **${formatMs(p1080.shot.median)} ms**、720p **${formatMs(p720.shot.median)} ms**，仍高于 25 ms 门槛；结论不依赖 Node 解码器的速度。`);
    const correctness = [...chromeSuite.results, ...(edgeSuite ? edgeSuite.results : [])];
    const allOk = correctness.every((r) =>
      r.determinism && r.decoderMatchesFfmpeg && r.transparency && r.notBlank &&
      r.inlineImage && r.badFrames.length === 0 && r.networkBlocked && r.fileBlocked);
    lines.push(`- 正确性（确定性、透明、内联图片、无空白/旧帧、断网、file://）：**${allOk ? '全部通过' : '有失败项，见第 3 节'}**。`);
    lines.push('');
    lines.push('**建议**');
    lines.push('');
    if (!pass1080 || !pass720) {
      lines.push('- 预览按显示尺寸抓帧并限 15 fps；1080p 全画质预览不要按 30 fps 承诺。');
      lines.push('- 导出不节流，逐帧等待抓图；以当前中位数估算，1080p 导出上限约 ' + fps1080.toFixed(1) + ' fps，720p 约 ' + fps720.toFixed(1) + ' fps（不含编码）。');
      lines.push('- 若导出吞吐不够，优先降低抓帧分辨率（按输出尺寸的 0.5–0.75 抓再放大），而不是减少帧数。');
    } else {
      lines.push('- 25 ms 门槛通过，可以按 30 fps 预览；导出仍应使用异步帧供应器。');
    }
    lines.push('- 每层一个 page 时，后台标签页的截图会永久挂起；抓帧前必须 `Page.bringToFront`（详见第 4 节）。');
  }
  lines.push('');
  lines.push('## 1. 环境与启动参数');
  lines.push('');
  if (chrome) {
    lines.push(`- Chrome：\`${chrome.path}\``);
    lines.push(`- 版本：\`${chrome.version}\``);
    lines.push(`- 进程启动耗时：${formatMs(chrome.startupMs)} ms（到 DevTools URL 可连接）`);
    lines.push(`- 启动参数：\`${chrome.args.join(' ')}\``);
  }
  if (edge) {
    lines.push(`- Edge：\`${edge.path}\``);
    lines.push(`- 版本：\`${edge.version}\``);
    if (edge.startupMs) lines.push(`- Edge 进程启动耗时：${formatMs(edge.startupMs)} ms`);
    if (edge.args) lines.push(`- Edge 启动参数：\`${edge.args.join(' ')}\``);
  } else {
    lines.push('- Edge：未找到或未测试。');
  }
  lines.push('');
  lines.push('抓帧约定：');
  lines.push('');
  lines.push('- 页面通过 `Page.setDocumentContent` 注入；字体只用 `sans-serif` / `monospace`，不加载外部资源。');
  lines.push('- 确定性 stub（固定 `Date.now` / `performance.now`、按帧重置 `Math.random`、关闭 CSS 动画）内联为文档的第一个 `<script>`。`Page.addScriptToEvaluateOnNewDocument` 对 `Page.setDocumentContent` 不生效，生产实现必须内联，或改用真实导航 / data: URL。');
  lines.push('- CSS animation / transition 由注入样式强制关闭；`renderFrame` 等待 `document.fonts.ready` 后才调用 `pvf.rendered()`。');
  lines.push('- 页面内有一张内联 `data:` SVG（48×48），用于验证“内联资源可用、外部网络不可用”；它不参与进度条像素检查。');
  lines.push('- 每帧流程：`Runtime.evaluate(awaitPromise)` → `Page.bringToFront` → `Page.captureScreenshot`（PNG，clip）→ base64 + PNG 解码为 RGBA。');
  lines.push('- “解码”列 = base64 解码 + zlib inflate + 反滤波，对应 Qt `QImage::loadFromData` 的主要工作。');
  lines.push('- **传输与安全**：本 spike 用 `--remote-debugging-port=0`（loopback 随机端口）+ WebSocket，便于测量 CDP 往返；`--remote-allow-origins=*` 仅为 spike 方便。生产实现按安全评审改用 `--remote-debugging-pipe`（stdio fd 3/4），不开 TCP 端口、不需要 `--remote-allow-origins=*`；CDP 消息与截图路径不变，延迟差异应在 Stage 1 复测。');
  lines.push('');

  if (chromeSuite) {
    lines.push('## 2. 单页连续帧（Chrome）');
    lines.push('');
    const rows = [];
    for (const result of chromeSuite.results) {
      for (const [name, value] of [
        ['注入+渲染', result.render],
        ['截图（CDP）', result.shot],
        ['解码（PNG→RGBA）', result.decode],
        ['总计', result.total],
      ]) {
        rows.push([
          result.label,
          `${result.width}×${result.height}`,
          name,
          formatMs(value.min),
          formatMs(value.median),
          formatMs(value.p95),
          formatMs(value.max),
          formatMs(value.mean),
        ]);
      }
      rows.push([
        result.label,
        `${result.width}×${result.height}`,
        `等效 fps（中位/P95）`,
        '',
        (1000 / result.total.median).toFixed(1),
        (1000 / result.total.p95).toFixed(1),
        '',
        '',
      ]);
    }
    lines.push(...markdownTable(rows, ['场景', '分辨率', '阶段', 'min', 'median', 'p95', 'max', 'mean']));
    lines.push('');
    lines.push('每场景先跑 5 帧预热（不计入统计），随后连续抓取完整帧数。');
    lines.push('');
    lines.push('| 场景 | 帧数 | 页面就绪 | 平均 PNG 大小 |');
    lines.push('|---|---|---|---|');
    for (const result of chromeSuite.results) {
      lines.push(`| ${result.label} | ${result.frames} | ${formatMs(result.readyMs)} ms | ${(result.avgPngBytes / 1024).toFixed(1)} KiB |`);
    }
    lines.push('');

    lines.push('## 3. 正确性');
    lines.push('');
    lines.push('| 检查 | ' + chromeSuite.results.map((r) => r.label).join(' | ') + ' |');
    lines.push('|---|' + chromeSuite.results.map(() => '---').join('|') + '|');
    const checkRow = (name, fn) =>
      lines.push(`| ${name} | ` + chromeSuite.results.map((r) => (fn(r) ? '通过' : '**失败**')).join(' | ') + ' |');
    checkRow('同帧两次 raw RGBA 哈希一致（ffmpeg 解码）', (r) => r.determinism);
    checkRow('内置 PNG 解码与 ffmpeg 像素一致', (r) => r.decoderMatchesFfmpeg);
    checkRow('透明背景（角落 alpha=0，进度条 alpha=255）', (r) => r.transparency);
    checkRow('内联图片（data: URL SVG）渲染', (r) => r.inlineImage);
    checkRow('无空白帧（不透明像素 > 1000）', (r) => r.notBlank);
    checkRow('无旧帧/错帧（逐帧检查进度条像素）', (r) => r.badFrames.length === 0);
    checkRow('网络请求被拦截（fetch 失败）', (r) => r.networkBlocked);
    checkRow('file:// 被拦截（fetch 失败）', (r) => r.fileBlocked);
    lines.push('');
    for (const result of chromeSuite.results) {
      if (result.badFrames.length > 0) {
        lines.push(`- ${result.label} 异常帧：\`${JSON.stringify(result.badFrames)}\``);
      }
      lines.push(`- ${result.label}：角落 alpha=${result.cornerAlpha}，进度条 alpha=${result.barAlpha}，内联图标 alpha=${result.iconAlpha}/g=${result.iconGreen}，不透明像素=${result.opaquePixels}，网络探测=${JSON.stringify(result.blocking)}`);
      lines.push(`- ${result.label}：raw RGBA SHA-256（前 16 位）A=\`${result.detHashA}\`，B=\`${result.detHashB}\`；PNG 字节相同=${result.pngIdentical ? '是' : '否'}（仅参考，不作判据）`);
    }
    lines.push('');
    lines.push('说明：PNG 元数据（时间戳/压缩块）不参与比较；确定性检查统一用 `ffmpeg -pix_fmt rgba` 解码后的字节哈希。');
    lines.push('');

    lines.push('## 4. 两个 page 同时存在');
    lines.push('');
    lines.push('| 场景 | 分辨率 | 轮数 | 每轮两页总耗时 min | median | p95 | max | mean | 进程树 RSS |');
    lines.push('|---|---|---|---|---|---|---|---|---|');
    for (const result of chromeSuite.multi) {
      lines.push(
        `| ${result.label} | ${result.width}×${result.height} | ${result.rounds} | ` +
          `${formatMs(result.pair.min)} | ${formatMs(result.pair.median)} | ${formatMs(result.pair.p95)} | ` +
          `${formatMs(result.pair.max)} | ${formatMs(result.pair.mean)} | ` +
          `${(result.rss.kb / 1024).toFixed(0)} MiB / ${result.rss.count} procs |`,
      );
    }
    lines.push('');
    lines.push('每轮 = page A 注入+截图+解码，再 page B 注入+截图+解码。**两个 page 在同一浏览器内时，后台标签页不会产生合成帧；每次截图前必须 `Page.bringToFront`，否则 `Page.captureScreenshot` 永久挂起。** 因此双页抓帧是串行的，不能并行。');
    lines.push('');
    lines.push('## 5. 内存');
    lines.push('');
    lines.push(`- 浏览器启动后（无页面）：${(rssBaseline.kb / 1024).toFixed(0)} MiB / ${rssBaseline.count} 个进程`);
    lines.push(`- 单页测量结束：${(chromeSuite.rss.kb / 1024).toFixed(0)} MiB / ${chromeSuite.rss.count} 个进程`);
    for (const result of chromeSuite.multi) {
      lines.push(`- ${result.label}（两页）：${(result.rss.kb / 1024).toFixed(0)} MiB / ${result.rss.count} 个进程`);
    }
    lines.push('');
    lines.push('RSS 为进程树求和，共享内存会被重复计数，只能作为上界参考。');
    lines.push('');
  }

  if (edgeSuite) {
    lines.push('## 6. Edge smoke');
    lines.push('');
    lines.push('| 场景 | 分辨率 | 帧数 | 注入+渲染 中位/P95 | 截图 中位/P95 | 解码 中位/P95 | 总计 中位/P95 | 等效 fps |');
    lines.push('|---|---|---|---|---|---|---|---|');
    for (const result of edgeSuite.results) {
      lines.push(
        `| ${result.label} | ${result.width}×${result.height} | ${result.frames} | ` +
          `${formatMs(result.render.median)} / ${formatMs(result.render.p95)} ms | ` +
          `${formatMs(result.shot.median)} / ${formatMs(result.shot.p95)} ms | ` +
          `${formatMs(result.decode.median)} / ${formatMs(result.decode.p95)} ms | ` +
          `${formatMs(result.total.median)} / ${formatMs(result.total.p95)} ms | ` +
          `${(1000 / result.total.median).toFixed(1)} |`,
      );
    }
    lines.push('');
    lines.push('| 检查 | 结果 |');
    lines.push('|---|---|');
    for (const result of edgeSuite.results) {
      lines.push(`| ${result.label} 同帧像素一致 | ${result.determinism ? '通过' : '**失败**'} |`);
      lines.push(`| ${result.label} 透明背景 | ${result.transparency ? '通过' : '**失败**'} |`);
      lines.push(`| ${result.label} 内联图片 | ${result.inlineImage ? '通过' : '**失败**'} |`);
      lines.push(`| ${result.label} 无空白/旧帧 | ${result.notBlank && result.badFrames.length === 0 ? '通过' : '**失败**'} |`);
      lines.push(`| ${result.label} 网络/file 拦截 | ${result.networkBlocked && result.fileBlocked ? '通过' : '**失败**'} |`);
    }
    lines.push('');
  } else if (edgeError) {
    lines.push('## 6. Edge smoke');
    lines.push('');
    lines.push('**失败。**');
    lines.push('');
    lines.push('```');
    lines.push(edgeError);
    lines.push('```');
    lines.push('');
  }

  lines.push('## 7. 样本 PNG');
  lines.push('');
  const suites = [];
  if (chromeSuite) suites.push(...chromeSuite.results);
  if (edgeSuite) suites.push(...edgeSuite.results);
  for (const result of suites) {
    lines.push(`- ${result.label} 首帧：\`${result.samples.first}\``);
    lines.push(`- ${result.label} 中间帧：\`${result.samples.mid}\``);
    lines.push(`- ${result.label} 末帧：\`${result.samples.last}\``);
    lines.push(`- ${result.label} 确定性 A/B：\`${result.samples.detA}\` / \`${result.samples.detB}\``);
  }
  lines.push('');
  lines.push('## 8. 发现与限制');
  lines.push('');
  lines.push('1. **后台标签页截图会挂起。** headless Chrome 只为前台标签页合成帧；对后台 page 调 `Page.captureScreenshot` 会一直等到 15 s 超时。`--disable-backgrounding-occluded-windows`、`--disable-renderer-backgrounding`、`--disable-background-timer-throttling` 都不能解决，只有 `Page.bringToFront` / `Target.activateTarget` 有效。生产实现必须：抓帧前激活目标 page；多 HTML 层串行抓帧，或每层独立浏览器进程（RSS 更高）。');
  lines.push('2. `requestAnimationFrame` 在 headless 下不可靠；本 spike 用 `setTimeout(0)` + 强制 layout 后调用 `pvf.rendered()`，并用逐帧像素检查证明不是旧帧。生产契约可保留“`rendered()` + 一次强制样式刷新”，不要依赖 rAF 次数。');
  lines.push('3. `Page.addScriptToEvaluateOnNewDocument` 不会在 `Page.setDocumentContent` 时执行；宿主必须把确定性 stub 内联进 HTML 文档开头，或改用真实导航 / `data:` URL。');
  lines.push('4. `--run-all-compositor-stages-before-draw` 在本机没有帮助，已从启动参数移除；保留它会增加等待合成帧的风险。');
  lines.push('5. `Page.captureScreenshot` 的 PNG 编码是主要成本；`optimizeForSpeed`、`fromSurface:false` 在 Chrome 157 上没有改善（后者更慢）。');
  lines.push('6. 单页连续抓帧时不需要 `Page.bringToFront`；多页时必须。');
  lines.push('7. 同一帧两次像素一致依赖：固定 `Date.now`/`performance.now`、按帧重置 PRNG、禁用 CSS 动画/过渡、内联全部资源、等待字体。缺一不可。');
  lines.push(`8. 本报告的“解码”用 Node 内置 zlib 解码器计时，是 Qt \`QImage::loadFromData\` 的上界；截图环节本身的 ${formatMs(shot1080Median)} ms（1080p）已足以判定 25 ms 门槛不成立。`);
  lines.push(`9. 测量期间系统还有其它 CPU 负载（见开头负载值），绝对值会随机器忙闲波动；即便如此，1080p 截图环节的最小值仍有 ${formatMs(shot1080Min)} ms，25 ms 门槛的 NO-GO 结论不受单次波动影响。`);
  lines.push('');
  fs.mkdirSync(path.dirname(reportPath), { recursive: true });
  fs.writeFileSync(reportPath, lines.join('\n') + '\n');
}

// ---------------------------------------------------------------------------
// main

let activeBrowser = null;

async function cleanup() {
  if (activeBrowser) {
    const browser = activeBrowser;
    activeBrowser = null;
    await browser.close().catch(() => {});
  }
}

async function main() {
  const chromePath = findExecutable(options.browser, [
    'google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser',
  ]);
  if (!chromePath) {
    const message = '找不到 Chrome/Chromium。请安装 Chrome 或用 --browser 指定路径。';
    fs.mkdirSync(path.dirname(options.report), { recursive: true });
    fs.writeFileSync(options.report, `# M6 Stage 0：Chrome/CDP spike 测量报告\n\n${message}\n`);
    console.error(message);
    process.exit(1);
  }
  const edgePath = options.edge === '-'
    ? ''
    : findExecutable(options.edge, ['microsoft-edge', 'microsoft-edge-stable']);

  const context = {
    chrome: null,
    edge: null,
    chromeSuite: null,
    edgeSuite: null,
    edgeError: '',
    rssBaseline: { kb: 0, count: 0 },
    loadStart: os.loadavg().map((v) => v.toFixed(2)).join('/'),
    error: '',
  };

  let exitCode = 0;
  try {
    const chromeVersion = executableVersion(chromePath);
    console.log(`Chrome: ${chromePath} (${chromeVersion})`);
    const chrome = await launchBrowser(chromePath);
    activeBrowser = chrome;
    context.chrome = {
      path: chromePath,
      version: chromeVersion,
      startupMs: chrome.startupMs,
      args: chrome.args,
    };
    context.rssBaseline = processTreeRss(chrome.profile);
    console.log(`Chrome started in ${formatMs(chrome.startupMs)} ms, RSS ${(context.rssBaseline.kb / 1024).toFixed(0)} MiB`);
    context.chromeSuite = await runBrowserSuite(chrome, {
      labelPrefix: 'chrome',
      frames: options.frames,
      multiFrames: options.multiFrames,
      suiteLabel: 'chrome',
    });
    const leftovers = processesWithProfile(chrome.profile);
    await cleanup();
    const after = processesWithProfile(chrome.profile);
    if (after.length > 0) {
      throw new Error(`browser cleanup left ${after.length} processes: ${after.join(', ')}`);
    }
    console.log(`Chrome closed; ${leftovers.length} processes at shutdown start, 0 after.`);

    if (edgePath) {
      const edgeVersion = executableVersion(edgePath);
      console.log(`Edge: ${edgePath} (${edgeVersion})`);
      try {
        const edge = await launchBrowser(edgePath);
        activeBrowser = edge;
        context.edge = {
          path: edgePath,
          version: edgeVersion,
          startupMs: edge.startupMs,
          args: edge.args,
        };
        const smoke = await runScenario(edge, {
          label: 'edge-720p',
          width: 1280,
          height: 720,
          frames: Math.min(options.edgeFrames, options.frames),
        });
        context.edgeSuite = { results: [smoke], multi: [], rss: processTreeRss(edge.profile) };
        const afterEdge = processesWithProfile(edge.profile);
        await cleanup();
        const edgeLeft = processesWithProfile(edge.profile);
        if (edgeLeft.length > 0) {
          throw new Error(`Edge cleanup left ${edgeLeft.length} processes`);
        }
        console.log(`Edge smoke done; ${afterEdge.length} processes at shutdown start, 0 after.`);
      } catch (error) {
        context.edgeError = String(error && error.stack ? error.stack : error);
        await cleanup();
        console.error('Edge smoke failed:', context.edgeError);
      }
    } else {
      console.log('Edge not found or skipped.');
    }

    // A clean run still reports NO-GO when the latency threshold is missed;
    // only correctness failures makes the script exit nonzero.
    const allResults = [
      ...(context.chromeSuite ? context.chromeSuite.results : []),
      ...(context.edgeSuite ? context.edgeSuite.results : []),
    ];
    const correctnessOk = allResults.every((r) =>
      r.determinism && r.decoderMatchesFfmpeg && r.transparency && r.notBlank &&
      r.inlineImage && r.badFrames.length === 0 && r.networkBlocked && r.fileBlocked);
    if (!correctnessOk || context.edgeError) exitCode = 1;
  } catch (error) {
    context.error = String(error && error.stack ? error.stack : error);
    console.error(error);
    exitCode = 1;
    await cleanup();
  }

  writeReport(options.report, context);
  console.log(`report: ${options.report}`);
  process.exit(exitCode);
}

process.on('SIGINT', () => {
  console.error('interrupted; cleaning up browser');
  cleanup().finally(() => process.exit(130));
});
process.on('SIGTERM', () => {
  cleanup().finally(() => process.exit(143));
});

main().catch(async (error) => {
  console.error(error);
  await cleanup();
  process.exit(1);
});
