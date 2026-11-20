import { shell, systemPreferences } from 'electron';
import type { PermissionInfo, PermissionStatus } from '@zeyvola/protocol';
import { tm } from './settings';

function mapStatus(s: string): PermissionStatus {
  switch (s) {
    case 'granted': return 'granted';
    case 'denied': return 'denied';
    case 'restricted': return 'restricted';
    case 'not-determined': return 'not-determined';
    default: return 'unknown';
  }
}

export function getPermissions(): PermissionInfo {
  if (process.platform === 'darwin') {
    return {
      screen: mapStatus(systemPreferences.getMediaAccessStatus('screen')),
      camera: mapStatus(systemPreferences.getMediaAccessStatus('camera')),
      microphone: mapStatus(systemPreferences.getMediaAccessStatus('microphone')),
      // Mouse events via a global monitor need no extra permission on macOS.
      input: 'granted',
    };
  }
  if (process.platform === 'win32') {
    return {
      screen: 'granted',
      camera: mapStatus(systemPreferences.getMediaAccessStatus('camera')),
      microphone: mapStatus(systemPreferences.getMediaAccessStatus('microphone')),
      input: 'granted',
    };
  }
  if (process.platform === 'linux') {
    // X11 screen capture and the pointer helper need no permission. On Wayland the screen comes
    // through xdg-desktop-portal, which asks the user on the first recording of each session.
    const wayland = (process.env.XDG_SESSION_TYPE ?? '').toLowerCase() === 'wayland';
    const media = (kind: 'camera' | 'microphone'): PermissionStatus => {
      try { return mapStatus(systemPreferences.getMediaAccessStatus(kind)); } catch { return 'unknown'; }
    };
    return { screen: wayland ? 'unknown' : 'granted', camera: media('camera'), microphone: media('microphone'), input: 'granted' };
  }
  return { screen: 'unknown', camera: 'unknown', microphone: 'unknown', input: 'unknown' };
}

export async function requestPermission(kind: 'camera' | 'microphone' | 'screen'): Promise<boolean> {
  // Linux: no programmatic request. Wayland's portal dialog appears with the first capture; camera
  // and microphone access is decided by the desktop environment (PipeWire), usually without a prompt.
  if (process.platform === 'linux') return kind === 'screen' ? (process.env.XDG_SESSION_TYPE ?? '').toLowerCase() !== 'wayland' : true;
  if (process.platform !== 'darwin') return true;
  if (kind === 'screen') {
    // macOS has no programmatic request for screen recording; the first capture prompts.
    return systemPreferences.getMediaAccessStatus('screen') === 'granted';
  }
  return systemPreferences.askForMediaAccess(kind);
}

const MAC_PANES: Record<string, string> = {
  screen: 'x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture',
  camera: 'x-apple.systempreferences:com.apple.preference.security?Privacy_Camera',
  microphone: 'x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone',
  input: 'x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent',
};

const WIN_PANES: Record<string, string> = {
  camera: 'ms-settings:privacy-webcam',
  microphone: 'ms-settings:privacy-microphone',
  screen: 'ms-settings:privacy-graphicscaptureprogrammatic',
  input: 'ms-settings:privacy',
};

/**
 * Opens the OS privacy pane so the user can change the setting themselves.
 * Linux has no such pane: screen capture is granted by the desktop portal (Wayland) or needs no
 * permission at all (X11), and camera/microphone access is decided by the session's audio server.
 * There is therefore nothing to open, and saying so is better than a link that does nothing.
 */
export async function openPermissionSettings(kind: string): Promise<void> {
  if (process.platform === 'linux') {
    const wayland = (process.env.XDG_SESSION_TYPE ?? '').toLowerCase() === 'wayland';
    if (kind === 'screen' && wayland) {
      // Nothing to open: the portal asks when a recording starts. The dialog returns the request
      // to this window, so the user simply starts the recording.
      throw new Error(tm("Wayland'de ekran izni kayıt başlarken masaüstünün paylaşım penceresinde sorulur; açılacak bir ayar penceresi yok."));
    }
    throw new Error(tm("Linux'ta ekran, kamera ve mikrofon için ayrı bir sistem izin penceresi yok; aygıtlar kayıt başlarken açılır."));
  }
  const url = process.platform === 'darwin' ? MAC_PANES[kind] : process.platform === 'win32' ? WIN_PANES[kind] : undefined;
  if (url) await shell.openExternal(url);
}
