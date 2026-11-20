/**
 * Which kind of Linux session this is. Wayland hides the screen from applications: the source
 * list cannot be read and the screen only arrives through xdg-desktop-portal, whose own dialog
 * asks the user what to share. Every Linux code path that has to behave differently reads this
 * one function, so the check is written once (X11 is the default when nothing says Wayland).
 */
export function waylandSession(platform: string = process.platform, env: NodeJS.ProcessEnv = process.env): boolean {
  if (platform !== 'linux') return false;
  const type = (env.XDG_SESSION_TYPE ?? '').toLowerCase();
  if (type === 'wayland') return true;
  if (type === 'x11') return false;
  return !!env.WAYLAND_DISPLAY;
}
