# Zeyvola ses yardımcısı — Linux (`zeyvola-ses`)

İki iş yapar, ikisi de GStreamer ile:

```bash
zeyvola-ses --system-audio <çıktı.webm>                  # bilgisayarın kendi sesi (kayıt sürerken)
zeyvola-ses --extract-audio <video> <çıktı.webm>         # içe aktarılan videonun sesi
```

## Derleme

```bash
bash native/ses-linux/build.sh      # ya da: pnpm native:linux
```

Gerekli: `libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev`, çalışma zamanında
`gstreamer1.0-plugins-good` (opusenc, webmmux), `gstreamer1.0-plugins-base` (audioconvert,
uridecodebin) ve sistemde AAC çözücüsü için `gstreamer1.0-libav` (bkz. aşağıda).

## Neden ayrı yardımcı

Uygulama, Electron'un **tescilli codec içermeyen** `libffmpeg` derlemesini taşıyor: AAC çözücüsü
yok. Telefon/kamera videolarının sesi neredeyse hep AAC olduğu için, macOS'ta AVFoundation,
Windows'ta Media Foundation devreye giriyor; Linux'ta bu işi sistemin GStreamer'ı yapıyor.
Çıktı **Opus/WebM**: uygulamanın kendi ses izleriyle aynı kap, ayrıca çözücü gerektirmez.

## Sistem sesi

`pipewiresrc target-object=@DEFAULT_MONITOR@` (yedek: `pulsesrc device=@DEFAULT_MONITOR@`) →
`audioconvert` → `audioresample` → `opusenc` → `webmmux` → dosya. Varsayılan çıkışın **monitor**
kaynağıdır, yani hoparlöre giden ses; mikrofon değil.

Uygulama `stop` yazınca ya da stdin kapanınca boru hattına EOS gönderilir; muxer dosya sonunu
yazar, süreç çıkar. Bu, bir çökmede bile dosyanın okunabilir kalması içindir (`webm-probe.ts`).

## `--extract-audio`

`uridecodebin caps=audio/x-raw` → `audioconvert` → `audioresample` → `opusenc` → `webmmux`.

stdout'a tek satır:

```
{"type":"audio","audio":true,"converted":true,"from":"video"}
{"type":"audio","audio":false,"error":"no-audio"}
{"type":"audio","audio":true,"error":"decode"}
```

`decode`, sesin var olduğu ama okunamadığı anlamına gelir (sistemde o codec'in çözücüsü yok):
uygulama sessizce ses kaybetmek yerine kullanıcıya uyarı gösterir. AAC için
`gstreamer1.0-libav` (ya da `gstreamer1.0-plugins-bad` içindeki `faad`) gerekir; `.deb` bunu
`Recommends` olarak istiyor.

## Kendini deneme

```bash
native/ses-linux/bin/zeyvola-ses --self-test
# {"type":"selftest","ok":true,"opusenc":true,"pipewire":true}
```

## Bilinen sınırlar

- Wayland'de portal **ekran** sesi akışını ayrı bir düğüm olarak verebilir; bu yardımcı şimdilik
  varsayılan çıkışın monitor kaynağını kullanır (X11 ve PulseAudio/PipeWire oturumlarında doğru
  sonuç verir).
- Sesi olmayan bir videoda GStreamer "uygun eklenti yok" hatası da verebilir (video codec'i
  çözülemediğinde); iki durum da `decode` olarak raporlanır ve kullanıcıya görünür.
