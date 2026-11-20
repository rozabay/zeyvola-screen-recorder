# Zeyvola imleç yardımcısı — Linux (`zeyvola-imlec`)

Zeyvola uygulaması kayıt sırasında başlatır. Yalnızca **fare düğmeleri ve kaydırmayı** bildirir:
video, ses kaydetmez, **klavyeyi hiç okumaz**, diske hiçbir şey yazmaz, ağ isteği yapmaz. Tıklama
verisi, düzenleyicideki otomatik zoom önerileri için kullanılır.

macOS yardımcısıyla (`native/input-macos`) **aynı satır protokolünü** konuşur, bu yüzden
`apps/desktop/src/main/capture/input-helper.ts` değişmeden okur.

## Derleme

```bash
bash native/imlec-linux/build.sh      # ya da: pnpm native:linux
```

Gerekli geliştirme paketleri: `libx11-dev libxi-dev libxext-dev libxfixes-dev`, Wayland için
`libgio-2.0-dev libei-dev`. Çıktı: `bin/zeyvola-imlec`.

## Protokol

```
{"type":"ready","now":<s>}
{"type":"window","at":<s>,"now":<s>,"gx":<pt>,"gy":<pt>,"w":<pt>,"h":<pt>}   (yalnız X11, --window)
{"e":"d"|"u","b":"l"|"r"|"o","at":<s>,"now":<s>,"gx":<pt>,"gy":<pt>}
{"e":"s","dy":<pt>,"at":<s>,"now":<s>,"gx":<pt>,"gy":<pt>}
{"type":"error","code":"display"|"monitor"|"portal"}
```

`at` olay anı, `now` yazıldığı an; ikisi de açılıştan beri saniye (`CLOCK_MONOTONIC`). Uygulama
bunları kendi kayıt saatine çevirir. `gx/gy` ekran düzeninde sol üstten global piksel — Electron
ekran koordinatlarıyla aynı.

Tekerlek: yukarı `dy=+1`, aşağı `dy=-1` (macOS yardımcısının işaretiyle aynı).

## Oturum türüne göre

| | Nasıl | İzin |
|---|---|---|
| **X11** | `XInput2` ham olayları (`XI_RawButtonPress/Release`, `XI_RawMotion`); konum `XQueryPointer`. Ham olaylar **kök pencerede** seçilir (input-only pencerede sunucu `BadValue` verir). | Gerekmez |
| **Wayland** | `xdg-desktop-portal` **RemoteDesktop**: `CreateSession → SelectDevices(pointer) → Start → ConnectToEIS`. Düğmeler ve göreli hareket `libei` soketinden gelir; mutlak konum biriktirilir. | Kullanıcı bir kez onaylar |

Wayland'de pencere geometrisi okunamaz (`--window` yok sayılır); kayıt tüm ekran gibi davranır.
Portal yoksa ya da kullanıcı onaylamazsa `{"type":"error","code":"portal"}` yazılır ve **kayıt
tıklama verisi olmadan sürer** — macOS'ta yardımcı bulunmadığındaki davranışın aynısı.

Wayland yolunda **klavye aygıtı hiç seçilmez** (`SelectDevices` yalnız `pointer` bitini ister,
`types: 1`).

## Kendini deneme

```bash
native/imlec-linux/bin/zeyvola-imlec --self-test
# {"type":"selftest","ok":true,"session":"wayland","window":0}
```

Aygıt okumaz, izin istemez; yalnızca oturum türünü ve derlemenin neyi desteklediğini söyler.

## Bilinen sınırlar

- Wayland'de mutlak imleç konumu portal tarafından verilmez; konum, oturum açılışındaki sıfırdan
  göreli hareketle biriktirilir. İlk tıklama öncesi uzun bir hareket birikmişse konum kayabilir;
  zoom önerileri bu durumda tıklamalar arası **göreli** konumu doğru bulur, mutlak konum kayabilir.
- Xvfb gibi başsız sunucularda ham olaylar yalnızca kök pencerede seçilebilir; testler bunu
  kullanır (`tests/real-capture-linux`).
