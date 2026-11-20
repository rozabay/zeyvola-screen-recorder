# Zeyvola kayıt yardımcısı — Linux (`zeyvola-kayit`)

Uygulamanın Linux'taki yerel kaydedicisi. macOS'taki `zeyvola-recorder`'ın (ScreenCaptureKit)
karşılığı; **GStreamer** ile çalışır. Ekranı (imleç çizilmeden), kamerayı, mikrofonu ve sistem
sesini **ayrı dosyalara** yazar; hepsi tek saatte; `session.jsonl` çökme sonrası kaydı yeniden
kurmayı sağlar.

## Derleme

```bash
bash native/kayit-linux/build.sh      # ya da: pnpm native:linux
```

Gerekli: `libgstreamer1.0-dev libgstreamer-pbutils1.0-dev libgio-2.0-dev`, çalışma zamanında
`gstreamer1.0-plugins-good` (ximagesrc, vp9enc, opusenc, webmmux), `gstreamer1.0-plugins-base`,
ve Wayland için `gstreamer1.0-pipewire`.

## Protokol

macOS yardımcısıyla **birebir** aynı satır protokolü (`helper-client.ts`):

| Uygulama → yardımcı | Anlamı |
|---|---|
| `prepare {outDir, display, window, region, bounds, fps, quality, camera, mic, systemAudio, excludePid, meta}` | Kaynakları açar (`PAUSED`), kayıt başlamaz. `meta` değiştirilmeden `session.jsonl`'e yazılır. |
| `go` | Saati başlatır (`PLAYING`), `started` gönderir. |
| `pause` / `resume` / `mark` | Duraklatma (saat kesilir), devam, işaret. |
| `stop` | Dosyaları kapatır (`EOS`), `stopped` gönderir. |
| `cancel` | Hiçbir şey saklamadan kapatır. |

Yardımcı → uygulama: `hello`, `ready`, `started`, `status` (0,5 sn), `stopped`, `cancelled`,
`error {code, detail, fatal}`.

`stopped` satırı her dosyanın `startOffsetMs`, `durationMs` ve (varsa) `width/height/fps` ya da
`sampleRate/channels` değerlerini **kendisi** bildirir: uygulama dosyaları açıp ölçmek zorunda
kalmaz.

## Dosyalar

| Dosya | İçerik |
|---|---|
| `screen.webm` | Ekran, **imleç çizilmeden** (X11: `ximagesrc show-pointer=false`; Wayland: portal `cursor_mode=metadata`). VP9. |
| `camera.webm` | Kamera (`v4l2src`), VP9. |
| `mic.webm` | Mikrofon, Opus 48 kHz mono. |
| `system.webm` | Bilgisayarın kendi sesi (varsayılan çıkışın monitor kaynağı), Opus 48 kHz stereo. |
| `session.jsonl` | Oturum günlüğü: `session`, `track`, `pause`, `resume`, `mark`, `end`, `cancelled`. Her satır yazıldıktan sonra `fsync`. |
| `cursor.jsonl` | İmleç/tıklama günlüğü — `zeyvola-imlec` yazar (`input-helper.ts`). |

## Neden WebM (VP9 + Opus)

Linux'ta H.264 kodlayıcı **donanıma bağlıdır** (`vah264enc`, VA-API). Zeyvola hiçbir platformda
yazılım H.264 kodlayıcısı taşımıyor. Bu yüzden kayıt her zaman WebM: her yerde çalışır, patent
havuzu yok, uygulamanın kendi çözücüsüyle oynar. VA-API varsa dışa aktarma MP4'e gidebilir
(`export/route.ts` bunu çalışma anında ölçer).

`webmmux streamable=true`: dosya başlığı baştan yazılır, süre alanı boş kalır. Bir çökmede dosya
**okunabilir** kalır; uzunluğu `webm-probe.ts` bloklardan ölçer. Temiz kapanışta da (`EOS`) süre
alanı yazılmaz; bu bilinçli bir seçim, çökme güvenliği için.

## X11 ve Wayland

| | Ekran | İmleç |
|---|---|---|
| **X11** | `ximagesrc use-damage=false show-pointer=false` | Ayrı `cursor.jsonl` (`zeyvola-imlec`) |
| **Wayland** | `xdg-desktop-portal` **ScreenCast**: `CreateSession → SelectSources(monitor, cursor_mode=metadata) → Start → OpenPipeWireRemote` → `pipewiresrc` | Ayrı `cursor.jsonl` (`zeyvola-imlec`, portal RemoteDesktop) |

Wayland'de portal, kaynak seçimini **kendi diyaloğunda** yapar: "tüm ekran / pencere / bölge"
seçimi uygulamada değil, kullanıcının portal diyaloğundaki seçimindedir. Bölge kaydı yine çalışır
(tüm ekranın kırpılması). `parent_window` boş gönderilir; bazı masaüstleri diyaloğu bir pencereye
bağlamak isteyebilir, o durumda onay penceresi görünmeyebilir — bu portalın bilinen davranışıdır,
kayıt yardımcısının hatası değil.

## Kendini deneme ve dosya ölçümü

```bash
native/kayit-linux/bin/zeyvola-kayit --self-test          # VP9/Opus eklentileri var mı
native/kayit-linux/bin/zeyvola-kayit --probe-files <dir>  # boş liste: ölçüm uygulamada (webm-probe.ts)
```

`--probe-files` bilerek ölçüm yapmıyor: WebM dosyalarını uygulama zaten ölçüyor
(`manager.ts` → `probeWebmFolder`). Bu, yardımcıdan birkaç yüz satır EBML ayrıştırma kodunu kaldırır.

## Test

```bash
bash native/kayit-linux/build.sh
xvfb-run -a pnpm vitest run apps/desktop/src/main/capture/kayit-linux.test.ts
```

Test gerçek ikiliyi sürer: `prepare → go → mark → stop` ve `SIGKILL` ile çökme; sonra
`session.jsonl` + dosyalardan kaydın yeniden kurulabildiğini doğrular.

## Bilinen sınırlar

- Duraklatma şu an yalnızca **saati** durdurur (günlüğe `pause`/`resume` yazılır); GStreamer boru
  hatları akmaya devam eder. macOS'taki gibi kare atlamak için boru hatlarını `PAUSED`'a almak
  gerekir; bu, muxer'ın parça sınırlarını bozabildiği için ilk sürümde yapılmadı.
- Ses seviyesi göstergesi (`micLevel`) şimdilik 0 gönderilir; uygulama mikrofonu kendi
  göstergesiyle de izler.
- Sistem sesi varsayılan çıkışın monitor kaynağıdır; PipeWire'da `@DEFAULT_MONITOR@` çözümlenemezse
  PulseAudio yolu denenir.
