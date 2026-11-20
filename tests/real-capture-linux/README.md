# Linux gerçek kayıt denemesi

`tests/real-capture` (macOS) ve `tests/real-capture-web` (Windows) klasörlerinin Linux eşi.

Paketlenmiş uygulamayı **gerçek bir X görüntüsünde** açar, uygulamanın kendi smoke takımını
koşturur ve sonucu raporlar. Farkı: burada çalışan şey **paketten gelen** ikililerdir — paketleme
adımının derlediği `zeyvola-kayit`, `zeyvola-imlec` ve `zeyvola-ses` dahil.

```bash
pnpm native:linux
pnpm build
node apps/desktop/scripts/package.mjs --platform linux --arch x64 --skip-build

# Başsız (CI):
xvfb-run -a node tests/real-capture-linux/calistir.mjs apps/desktop/out/mp4/linux-x64

# Gerçek masaüstünde:
node tests/real-capture-linux/calistir.mjs apps/desktop/out/mp4/linux-x64
```

## Neler yapılır

1. Boş bir `HOME` hazırlanır (kendi projelerinize dokunulmaz).
2. Uygulama `--smoke` ile açılır: sentetik kamera/mikrofon, geçici proje klasörü.
3. Smoke takımı WebGL2 kompozitörü, gizleme efektlerini, WebCodecs kodlayıcılarını ve MP4/WebM
   yazıcılarını dener; rapor `--smoke-report` ile dosyaya yazılır.
4. Rapor okunur: `ok: true` değilse süreç 1 ile çıkar ve günlüğün sonu yazdırılır.

## Bayraklar

| Bayrak | Neden |
|---|---|
| `--no-sandbox` | `xvfb-run` içinde kullanıcı ad alanları kısıtlı olabilir; gerçek kurulumda `.deb` setuid `chrome-sandbox` taşır, bu bayrak gerekmez |
| `--use-gl=swiftshader` | Sanal ekranın GPU'su yok; SwiftShader yazılım WebGL2 sağlar |
| `--enable-unsafe-swiftshader` | Chromium 140+ SwiftShader'ı varsayılan olarak "güvensiz" sayıyor |

## Bilinen durum

GPU'suz bir `Xvfb` üzerinde smoke takımı **dakikalar** sürer (kareler yazılımla çizilir). CI'da
`ZEYVOLA_TEST_TIMEOUT_MIN` ile süre uzatılır; gerçek bir masaüstünde çok daha hızlıdır.
