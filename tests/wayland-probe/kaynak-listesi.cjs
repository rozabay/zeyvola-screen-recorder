// Reproduces the flow from the screenshot: open the app on the real session, list the sources the
// New Recording screen shows and report what a user would see. Read-only: starts no recording.
//   DISPLAY=:0 XDG_SESSION_TYPE=wayland npx electron tests/wayland-probe/kaynak-listesi.cjs
const { app, desktopCapturer, screen, nativeImage } = require('electron');

function wayland() {
  const t = (process.env.XDG_SESSION_TYPE ?? '').toLowerCase();
  if (t === 'wayland') return true;
  if (t === 'x11') return false;
  return !!process.env.WAYLAND_DISPLAY;
}

app.whenReady().then(async () => {
  const displays = screen.getAllDisplays();
  const out = { session: wayland() ? 'wayland' : 'x11', displays: displays.length, sources: [], source: 'desktopCapturer' };

  if (wayland()) {
    // The app's Wayland path: built from the displays, no portal prompt.
    out.source = 'sentetik (portal sormadan)';
    out.sources = displays.map((d, i) => ({
      id: `screen:${d.id}:0`,
      name: displays.length > 1 ? `Ekran ${i + 1} (${d.size.width}×${d.size.height})` : 'Tüm ekran',
      displayId: String(d.id),
      kind: 'display',
    }));
  } else {
    const list = await desktopCapturer.getSources({ types: ['screen', 'window'], thumbnailSize: { width: 64, height: 40 } });
    out.sources = list.map((s) => ({ id: s.id, name: s.name, displayId: s.display_id, kind: s.id.startsWith('screen:') ? 'display' : 'window', thumb: !s.thumbnail.isEmpty() }));
  }
  console.log(JSON.stringify(out, null, 1));
  app.exit(0);
});
