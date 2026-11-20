# Değişiklik günlüğü / Changelog

Bu dosya Zeyvola'nın sürümlerindeki önemli değişiklikleri listeler.

*This file lists notable changes in each Zeyvola release.*

## Yayımlanmamış — Linux desteği / Unreleased — Linux support

Linux (Debian, Ubuntu, Pardus) eklendi: `.deb` paketi, X11 ve Wayland'de yerel kayıt, ayrı izler,
tıklamadan otomatik zoom. macOS ve Windows davranışı değişmedi.

*Linux (Debian, Ubuntu, Pardus) added: a `.deb` package, native recording on X11 and Wayland,
separate tracks, click-driven zoom. macOS and Windows behaviour is unchanged.*

- **Kayıt yardımcısı `zeyvola-kayit`** (GStreamer): ekran **imleç çizilmeden**, kamera, mikrofon ve
  sistem sesi ayrı WebM dosyalarına, tek saatte. X11'de `ximagesrc`, Wayland'de
  `xdg-desktop-portal ScreenCast` + `pipewiresrc`. Çökme sonrası kurtarma macOS'la aynı
  `session.jsonl` biçimini kullanır. *Recording helper (GStreamer): the screen without the pointer,
  camera, microphone and system sound to separate WebM files on one clock. X11 uses `ximagesrc`,
  Wayland the ScreenCast portal. Crash recovery uses the same `session.jsonl` format as macOS.*
- **İmleç yardımcısı `zeyvola-imlec`**: X11'de XInput2 ham olayları, Wayland'de RemoteDesktop
  portalı + libei. Yalnız fare düğmeleri ve kaydırma; klavye hiç okunmaz. *Pointer helper: XInput2
  raw events on X11, the RemoteDesktop portal and libei on Wayland; mouse buttons and scrolling
  only, never the keyboard.*
- **Ses yardımcısı `zeyvola-ses`**: sistem sesi (varsayılan çıkışın monitor kaynağı) ve içe
  aktarılan videonun sesi (AAC → Opus, sistemin GStreamer'ıyla). *Sound helper: system audio and
  imported videos' sound (AAC → Opus through the system's GStreamer).*
- **`.deb` paketi**: `/usr/lib/zeyvola`, `/usr/bin/zeyvola`, masaüstü girdisi ve ikonlar;
  `chrome-sandbox` setuid root, bağımlılıklar GStreamer/PipeWire dahil. *`.deb` package with the
  desktop entry and icons; `chrome-sandbox` setuid root, GStreamer/PipeWire dependencies included.*
- **Kod denetimi**: `tools/codec-audit.mjs` artık ELF ikililerini okur, Linux yardımcılarının
  bağlandığı kitaplıkları listeler ve klavye okuma işaretlerini reddeder. *Codec audit now reads
  ELF binaries, lists the libraries the Linux helpers link and rejects keyboard-reading symbols.*
- **Dışa aktarma**: WebM (VP9 + Opus) her yerde; MP4 yalnız VA-API H.264 kodlayıcısı varsa, yoksa
  uygulama bunu önceden söyleyip WebM yazar. *Export: WebM everywhere; MP4 only where VA-API
  offers an H.264 encoder, otherwise the app says so and writes WebM.*
- **Bilinen sınırlar**: Wayland'de global kısayol ve pencere geometrisi yok (tepsi menüsü, "tüm
  ekran + kırpma"); duraklatma yalnız saati durdurur; `vah264enc` çoğu NVIDIA sisteminde yoktur.
  *Known limits: no global shortcuts or window geometry on Wayland (tray menu, whole screen
  cropped); pausing stops the clock only; `vah264enc` is absent on most NVIDIA systems.*

## 0.1.0 — 2026-11-13

İlk sürüm. macOS (Apple Silicon ve Intel) ve Windows 10/11 x64. Windows'ta otomatik testler GitHub'ın Windows makinesinde geçti; uygulama gerçek bir Windows bilgisayarda kurulup kayıt yapılarak denendi. Her donanımda denenmedi.

*First release. macOS (Apple silicon and Intel) and Windows 10/11 x64. On Windows, the automated tests passed on GitHub's Windows machine, and the app was installed and used for recording on a real Windows PC; it has not been tried on every kind of hardware.*

### Özellikler / Features
- Ekran, kamera, mikrofon ve sistem sesi kaydı (ayrı izler; macOS kayıt yardımcısı, ScreenCaptureKit). *Screen, camera, microphone and system audio recording on separate tracks (macOS recording helper, ScreenCaptureKit).*
- Tıklamalardan ve imleç hareketinden otomatik zoom önerileri; elle düzenlenebilir. *Automatic zoom suggestions from clicks and pointer movement, editable by hand.*
- Zaman çizelgesi: kesme, bölme, video ekleme (MP4, MOV, WebM). *Timeline: trim, split, insert video (MP4, MOV, WebM).*
- Gizleme: buzlu cam, bulanıklık ya da piksel efekti; dışa aktarılan her kare bu efektlerden geçer. *Privacy masks: frosted glass, blur or pixelate; every exported frame goes through them.*
- Büyüteç: seçilen aralıklarda imleci takip eden mercek. *Magnifier: a lens that follows the pointer in selected ranges.*
- Temalar, arka plan, kamera yerleşimi; 16:9, 9:16, 1:1, 4:5 formatları. *Themes, backgrounds, camera layout; 16:9, 9:16, 1:1 and 4:5 formats.*
- Dışa aktarma: MP4 (H.264 + AAC, işletim sisteminin kodlayıcısıyla; QuickTime ve Windows'un oynatıcısı açar) ve WebM (VP9 + Opus), 1080p veya 4K, 30 veya 60 fps. *Export: MP4 (H.264 + AAC through the operating system's encoders; opens in QuickTime and Windows' player) and WebM (VP9 + Opus), 1080p or 4K, 30 or 60 fps.*
- Dışa aktarılan dosyanın klasörü ve adı sistemin kaydetme penceresinden seçilir; son seçilen klasör hatırlanır. İptal ya da hata yarım dosya bırakmaz. *Choose the export's folder and name in the system save dialog; the last folder is remembered. Cancelling or a failure leaves no partial file.*
- Dışa aktarma sürerken bilgisayar uyumaz; pencere ya da uygulama kapanırsa dışa aktarma iptal edilir. *The computer stays awake during an export; closing the window or the app cancels it.*
- Türkçe ve İngilizce arayüz. *Turkish and English interface.*

### Düzeltmeler / Fixes
- Önceki geliştirme sürümlerinde "MP4" seçeneği VP9 + Opus yazıyordu; QuickTime ve Windows'un oynatıcısı bu dosyaları yalnızca ses olarak açıyordu. MP4 artık H.264 + AAC. *Earlier development builds wrote VP9 + Opus as "MP4", which QuickTime and Windows' player opened as sound only. MP4 is now H.264 + AAC.*

### Bilinen sınırlar / Known limits
- Kayıt ekranında "Yedek" (tarayıcı) yöntemi seçilirse sistem sesi kaydedilmez ve imleç videoya gömülür. *With the "Fallback" (browser) recording method, system audio is not recorded and the cursor is part of the video.*
- MP4 dışa aktarma için ekran kartının H.264 kodlayıcısı yoksa: Windows'ta Windows'un yazılım kodlayıcısı kullanılır (daha yavaş); o da yoksa video WebM olarak kaydedilir ve bu açıkça söylenir. *If the graphics card has no H.264 encoder: on Windows, Windows' software encoder is used (slower); otherwise the video is saved as WebM and the app says so.*
