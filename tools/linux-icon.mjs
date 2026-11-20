#!/usr/bin/env node
// Writes Zeyvola's app icon as a PNG file, for the Linux package (no image asset is kept in the
// repository: the mark is drawn by apps/desktop/src/main/png.ts). Bundled with the app's own
// esbuild on the fly, so the package icon is the same drawing the tray icon uses.
//
//   node tools/linux-icon.mjs <out.png> [size]
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
// esbuild is the desktop app's build dependency; resolve it from there, like the other tools do.
const require = createRequire(join(ROOT, 'apps', 'desktop', 'package.json'));
const { build } = await import(pathToFileURL(require.resolve('esbuild')).href);

const [outFile, sizeArg] = process.argv.slice(2);
if (!outFile) {
  console.error('kullanım: node tools/linux-icon.mjs <çıktı.png> [boyut]');
  process.exit(2);
}
const size = Number(sizeArg ?? 512);
if (!Number.isInteger(size) || size < 16 || size > 2048) {
  console.error('boyut 16 ile 2048 arasında bir tamsayı olmalı');
  process.exit(2);
}

const work = mkdtempSync(join(tmpdir(), 'zeyvola-ikon-'));
try {
  const entry = join(work, 'ikon.ts');
  writeFileSync(entry, `import { appIcon } from ${JSON.stringify(join(ROOT, 'apps', 'desktop', 'src', 'main', 'png.ts'))};\nexport const png = appIcon(${size});\n`);
  const bundle = join(work, 'ikon.mjs');
  await build({ entryPoints: [entry], outfile: bundle, bundle: true, platform: 'node', format: 'esm', logLevel: 'error' });
  const { png } = await import(pathToFileURL(bundle).href);
  writeFileSync(outFile, png);
  console.log(`İkon: ${outFile} (${size}×${size}, ${png.length} bayt)`);
} finally {
  rmSync(work, { recursive: true, force: true });
}
