// Drives the real Zeyvola package on a virtual X display: records a few seconds with the Linux
// helper, exports WebM, and checks the result with ffprobe. This is the Linux counterpart of
// tests/real-capture (macOS) and tests/real-capture-web (Windows).
//
//   node apps/desktop/scripts/package.mjs --platform linux --arch x64 --skip-build
//   xvfb-run -a node tests/real-capture-linux/calistir.mjs apps/desktop/out/mp4/linux-x64
//
// It starts the packaged app with --smoke (temporary projects folder, synthetic devices) and
// leans on the app's own report; the extra value here is that it runs the *packaged* files, with
// the helpers compiled by the package step, against a display that really exists.
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const pkg = process.argv[2];
if (!pkg || !existsSync(pkg)) {
  console.error('kullanım: node tests/real-capture-linux/calistir.mjs <paket klasörü>');
  process.exit(2);
}
if (!process.env.DISPLAY) {
  console.error('DISPLAY yok: xvfb-run -a ile çalıştırın');
  process.exit(2);
}

const exe = join(resolve(pkg), 'zeyvola');
for (const f of ['zeyvola', 'resources/bin/zeyvola-kayit', 'resources/bin/zeyvola-imlec', 'resources/bin/zeyvola-ses']) {
  if (!existsSync(join(resolve(pkg), f))) {
    console.error(`eksik: ${f} (paket tamam değil)`);
    process.exit(2);
  }
}

// A throwaway home, so the run never reads or writes the user's own projects.
const home = mkdtempSync(join(tmpdir(), 'zeyvola-linux-test-'));
const report = join(home, 'smoke.json');
const env = {
  ...process.env,
  HOME: home,
  XDG_CONFIG_HOME: join(home, '.config'),
  XDG_DATA_HOME: join(home, '.local', 'share'),
  XDG_CACHE_HOME: join(home, '.cache'),
  ZEYVOLA_TEST_HOME: home,
  ZEYVOLA_TEST_TIMEOUT_MIN: process.env.ZEYVOLA_TEST_TIMEOUT_MIN ?? '8',
};
mkdirSync(join(home, 'Videos'), { recursive: true });

console.log(`Zeyvola (paket) çalışıyor: ${exe}`);
const child = spawn(exe, [
  '--no-sandbox',
  // A virtual display has no GPU: SwiftShader is the software WebGL2 the app needs.
  '--use-gl=swiftshader',
  '--enable-unsafe-swiftshader',
  '--smoke',
  '--smoke-report',
  report,
], { env, stdio: ['ignore', 'pipe', 'pipe'] });

let log = '';
child.stdout.on('data', (d) => { log += String(d); });
child.stderr.on('data', (d) => { log += String(d); });

const code = await new Promise((res) => {
  const t = setTimeout(() => { child.kill('SIGKILL'); res(124); }, 9 * 60_000);
  child.on('exit', (c) => { clearTimeout(t); res(c ?? 1); });
});

let ok = code === 0;
let detail = '';
if (existsSync(report)) {
  const r = JSON.parse(readFileSync(report, 'utf8'));
  ok = ok && r.ok === true;
  detail = JSON.stringify({ ok: r.ok, steps: (r.steps ?? []).filter((s) => s.ok === false).map((s) => s.name) });
} else {
  ok = false;
  detail = 'rapor yazılmadı';
}

console.log(`çıkış=${code} ${detail}`);
if (!ok) {
  console.error(log.split('\n').slice(-40).join('\n'));
  console.error(`günlük: ${home}`);
  process.exit(1);
}
rmSync(home, { recursive: true, force: true });
console.log('Linux gerçek kayıt denemesi: tamam');
