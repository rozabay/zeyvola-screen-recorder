// Full ScreenCast handshake from Electron's main process, to find out whether the portal's
// dialog opens and what `parent_window` it needs on this desktop. Diagnostic only: changes nothing
// on disk, and the session it creates is closed when the process exits.
const { app, BrowserWindow } = require('electron');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const run = promisify(execFile);

const DEST = 'org.freedesktop.portal.Desktop';
const PATH = '/org/freedesktop/portal/desktop';

async function dbus(method, args) {
  return run('gdbus', ['call', '--session', '--dest', DEST, '--object-path', PATH, '--method', method, ...args], { timeout: 60_000 });
}

async function monitorResponse(requestPath, seconds) {
  // gdbus monitor prints signals; it is killed when the timeout ends.
  return new Promise((resolve) => {
    const child = execFile('gdbus', ['monitor', '--session', '--dest', DEST, '--object-path', requestPath], { timeout: seconds * 1000 });
    let out = '';
    child.stdout.on('data', (d) => { out += String(d); });
    child.stderr.on('data', (d) => { out += String(d); });
    child.on('exit', () => resolve(out));
    child.on('error', () => resolve(out));
  });
}

app.whenReady().then(async () => {
  const win = new BrowserWindow({ show: true, width: 520, height: 420 });
  await win.loadFile('/tmp/wl-test/sayfa.html');
  await new Promise((r) => setTimeout(r, 1000));

  const parent = process.env.ZEYVOLA_PARENT ?? '';
  console.log('parent_window =', JSON.stringify(parent));

  const token = `zeyvola_${process.pid}`;
  const created = await dbus('org.freedesktop.portal.ScreenCast.CreateSession', [`{'session_handle_token': <'${token}'>}`]);
  const requestPath = /objectpath '([^']+)'/.exec(created.stdout)?.[1];
  console.log('CreateSession istek yolu:', requestPath);

  const first = await monitorResponse(requestPath, 8);
  console.log('--- ilk yanıt (8 sn):');
  console.log(first.trim().slice(0, 900) || '(yanıt yok)');

  // If the response gave a session handle, go on to SelectSources + Start.
  const session = /session_handle[^)]*?'([^']+)'/.exec(first)?.[1];
  if (!session) { console.log('oturum tanıtıcısı gelmedi; duruyorum'); app.exit(0); return; }
  console.log('oturum:', session);

  const selected = await dbus('org.freedesktop.portal.ScreenCast.SelectSources', [session, `{'types': <uint32 3>}`, '']);
  console.log('SelectSources ->', selected.stdout.trim().slice(0, 200));

  const started = await dbus('org.freedesktop.portal.ScreenCast.Start', [session, parent, '{}']);
  const startReq = /objectpath '([^']+)'/.exec(started.stdout)?.[1];
  console.log('Start istek yolu:', startReq);

  const second = await monitorResponse(startReq, 45);
  console.log('--- Start yanıtı (45 sn):');
  console.log(second.trim().slice(0, 900) || '(YANIT YOK — diyalog acilmadi ya da onaylanmadi)');
  app.exit(0);
});
