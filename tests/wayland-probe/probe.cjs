// Diagnostic: what does desktopCapturer.getSources() return on this session?
//   npx electron tests/wayland-probe/probe.cjs
const { app, desktopCapturer, screen } = require('electron');

app.whenReady().then(async () => {
  const session = process.env.XDG_SESSION_TYPE ?? '(yok)';
  const displays = screen.getAllDisplays();
  const out = {
    session,
    waylandDisplay: process.env.WAYLAND_DISPLAY ?? null,
    displayServer: process.env.DISPLAY ?? null,
    ozonePlatform: process.argv.find((a) => a.startsWith('--ozone-platform')),
    electron: process.versions.electron,
    chrome: process.versions.chrome,
    displays: displays.map((d) => ({ id: d.id, bounds: d.bounds, scaleFactor: d.scaleFactor })),
    sources: null,
    error: null,
  };
  try {
    const list = await desktopCapturer.getSources({ types: ['screen', 'window'], thumbnailSize: { width: 64, height: 40 } });
    out.sources = list.map((s) => ({ id: s.id, name: s.name, displayId: s.display_id, thumb: !s.thumbnail.isEmpty() }));
  } catch (e) {
    out.error = String(e && e.message);
  }
  console.log(JSON.stringify(out, null, 1));
  app.exit(0);
});
