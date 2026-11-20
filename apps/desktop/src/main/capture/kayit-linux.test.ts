// End-to-end check of the Linux recording helper: builds nothing, but drives the real binary
// through its line protocol (helper-client.ts) and then rebuilds the recording from what reached
// the disk (recovery.ts + webm-probe.ts). Skipped when the helper has not been built.
//
//   bash native/kayit-linux/build.sh
//   pnpm vitest run apps/desktop/src/main/capture/kayit-linux.test.ts
//
// It records the screen of whatever DISPLAY points at (Xvfb in CI: `xvfb-run -a pnpm test`).
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { afterAll, describe, expect, it } from 'vitest';
import { msToTicks } from '@zeyvola/timing';
import { probeWebmFolder } from './webm-probe';
import { parseSessionLog, rebuildRecording, SESSION_FILE } from './recovery';
import type { ProbedFile } from './helper-client';

const HELPER = resolve(import.meta.dirname, '../../../../../native/kayit-linux/bin/zeyvola-kayit');
const X11 = process.platform === 'linux' && existsSync(HELPER) && !!process.env.DISPLAY;

const dirs: string[] = [];
afterAll(() => { for (const d of dirs) rmSync(d, { recursive: true, force: true }); });

describe.skipIf(!X11)('Linux recording helper (zeyvola-kayit)', () => {
  it('records each source into its own file and logs what reconstruction needs', async () => {
    const outDir = mkdtempSync(join(tmpdir(), 'zeyvola-kayit-test-'));
    dirs.push(outDir);
    const { spawn } = await import('node:child_process');
    const proc = spawn(HELPER, [], { stdio: ['pipe', 'pipe', 'pipe'] });
    const lines: Record<string, unknown>[] = [];
    let buffer = '';
    proc.stdout.setEncoding('utf8');
    proc.stdout.on('data', (chunk: string) => {
      buffer += chunk;
      let nl: number;
      while ((nl = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, nl);
        buffer = buffer.slice(nl + 1);
        try { lines.push(JSON.parse(line) as Record<string, unknown>); } catch { /* not JSON */ }
      }
    });
    const wait = async (type: string, timeoutMs = 30_000) => {
      const start = Date.now();
      while (Date.now() - start < timeoutMs) {
        const found = lines.find((l) => l.type === type);
        if (found) return found;
        await new Promise((r) => setTimeout(r, 50));
      }
      throw new Error(`${type} gelmedi (${lines.map((l) => l.type).join(', ')})`);
    };

    proc.stdin.write(`${JSON.stringify({
      cmd: 'prepare',
      outDir,
      display: 0,
      window: null,
      region: null,
      bounds: { x: 0, y: 0, w: 1280, h: 800 },
      fps: 30,
      quality: 'high',
      camera: null,
      mic: null,
      systemAudio: false,
      excludePid: process.pid,
      meta: {
        recordingId: 'rec_kayit0001',
        backend: 'native-linux',
        target: { kind: 'display', label: 'Xvfb', bounds: { x: 0, y: 0, w: 1280, h: 800 }, scaleFactor: 1 },
      },
    })}\n`);
    expect(await wait('hello')).toMatchObject({ type: 'hello' });
    await wait('ready');
    proc.stdin.write('{"cmd":"go"}\n');
    const started = await wait('started');
    expect(typeof started.startedAtMs).toBe('number');
    await new Promise((r) => setTimeout(r, 2500));
    proc.stdin.write('{"cmd":"mark"}\n');
    await new Promise((r) => setTimeout(r, 300));
    proc.stdin.write('{"cmd":"stop"}\n');
    const stopped = await wait('stopped');
    expect(stopped.durationMs).toBeGreaterThan(1500);
    proc.kill('SIGKILL');

    // The same files the app reads after a crash.
    const log = parseSessionLog(readFileSync(join(outDir, SESSION_FILE), 'utf8'));
    expect(log?.meta).toMatchObject({ backend: 'native-linux', recordingId: 'rec_kayit0001' });
    const parents = Object.keys((log?.meta?.target ?? {}) as object);
    expect(parents.length).toBeGreaterThan(0);
    const probed: ProbedFile[] = await probeWebmFolder(outDir, (log?.tracks ?? []).map((t) => t.name));
    const screen = probed.find((p) => p.name === 'screen.webm');
    expect(screen?.error).toBeUndefined();
    expect(screen?.width).toBe(1280);
    expect(screen?.height).toBe(800);

    const files = (log?.tracks ?? []).map((t) => t.name).concat(['session.jsonl']);
    const rebuilt = rebuildRecording({ recordingId: 'rec_kayit0001', log: log!, probed, files, platform: 'linux-x64' });
    expect(rebuilt).not.toBeNull();
    expect(rebuilt!.recording.capture.backend).toBe('native-linux');
    expect(rebuilt!.recording.capture.cursorInFrames).toBe(false);
    expect(rebuilt!.recording.sources.screen?.file).toBe('media/rec_kayit0001/screen.webm');
    expect(rebuilt!.recording.duration).toBeGreaterThan(msToTicks(1500));
    expect(rebuilt!.cleanEnd).toBe(true);
  }, 60_000);

  it('keeps a usable log when the process is killed mid-recording', async () => {
    const outDir = mkdtempSync(join(tmpdir(), 'zeyvola-kayit-crash-'));
    dirs.push(outDir);
    const { spawn } = await import('node:child_process');
    const proc = spawn(HELPER, [], { stdio: ['pipe', 'pipe', 'pipe'] });
    let buffer = '';
    const lines: Record<string, unknown>[] = [];
    proc.stdout.setEncoding('utf8');
    proc.stdout.on('data', (chunk: string) => {
      buffer += chunk;
      let nl: number;
      while ((nl = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, nl);
        buffer = buffer.slice(nl + 1);
        try { lines.push(JSON.parse(line) as Record<string, unknown>); } catch { /* not JSON */ }
      }
    });
    const wait = async (type: string) => {
      const start = Date.now();
      while (Date.now() - start < 20_000) {
        if (lines.some((l) => l.type === type)) return;
        await new Promise((r) => setTimeout(r, 50));
      }
      throw new Error(`${type} gelmedi`);
    };
    proc.stdin.write(`${JSON.stringify({
      cmd: 'prepare', outDir, display: 0, window: null, region: null,
      bounds: { x: 0, y: 0, w: 1280, h: 800 }, fps: 30, quality: 'high',
      camera: null, mic: null, systemAudio: false, excludePid: process.pid,
      meta: {
        recordingId: 'rec_crash0009', backend: 'native-linux',
        target: { kind: 'display', label: 'Xvfb', bounds: { x: 0, y: 0, w: 1280, h: 800 }, scaleFactor: 1 },
      },
    })}\n`);
    await wait('ready');
    proc.stdin.write('{"cmd":"go"}\n');
    await wait('started');
    await new Promise((r) => setTimeout(r, 2000));
    proc.kill('SIGKILL');
    await new Promise((r) => setTimeout(r, 300));

    const log = parseSessionLog(readFileSync(join(outDir, SESSION_FILE), 'utf8'));
    expect(log).not.toBeNull();
    expect(log!.meta?.backend).toBe('native-linux');
    // Killed before "stop": no end line, so recovery keeps what reached the disk.
    expect(log!.endDurationMs).toBeNull();
    expect(log!.tracks.length).toBeGreaterThan(0);
    const probed = await probeWebmFolder(outDir, log!.tracks.map((t) => t.name));
    const files = log!.tracks.map((t) => t.name).concat(['session.jsonl']);
    const rebuilt = rebuildRecording({ recordingId: 'rec_crash0009', log: log!, probed, files, platform: 'linux-x64' });
    if (rebuilt) {
      expect(rebuilt.cleanEnd).toBe(false);
      expect(rebuilt.recording.capture.backend).toBe('native-linux');
    }
  }, 60_000);
});
