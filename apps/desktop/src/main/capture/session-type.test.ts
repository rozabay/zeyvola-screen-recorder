import { describe, expect, it } from 'vitest';
import { waylandSession } from './session-type';

/**
 * The Wayland check decides two things that used to be wrong: which source list the New Recording
 * screen shows (the displays, without prompting the portal) and whether the native helper is
 * offered (it cannot be, since the portal dialog needs the app's own window).
 */
describe('waylandSession', () => {
  it('is true on a Wayland session and false on X11', () => {
    expect(waylandSession('linux', { XDG_SESSION_TYPE: 'wayland' })).toBe(true);
    expect(waylandSession('linux', { XDG_SESSION_TYPE: 'x11' })).toBe(false);
  });

  it('falls back to the display variables when the session type is not set', () => {
    expect(waylandSession('linux', { WAYLAND_DISPLAY: 'wayland-0' })).toBe(true);
    expect(waylandSession('linux', { DISPLAY: ':0' })).toBe(false);
  });

  it('never applies to another system', () => {
    expect(waylandSession('darwin', { XDG_SESSION_TYPE: 'wayland' })).toBe(false);
    expect(waylandSession('win32', { WAYLAND_DISPLAY: 'wayland-0' })).toBe(false);
  });

  it('ignores an empty or unknown session type', () => {
    expect(waylandSession('linux', { XDG_SESSION_TYPE: '' })).toBe(false);
    expect(waylandSession('linux', { XDG_SESSION_TYPE: 'tty' })).toBe(false);
    expect(waylandSession('linux', { XDG_SESSION_TYPE: 'WAYLAND' })).toBe(true);
  });
});
