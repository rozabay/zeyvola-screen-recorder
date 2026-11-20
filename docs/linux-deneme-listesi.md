# Zeyvola — Linux deneme listesi

Bu liste, Linux sürümünü gerçek donanımda deneyenler için. `docs/windows-deneme-listesi.md`'nin
eşi. Otomatik testler `xvfb-run -a pnpm test` ile koşar (`tests/real-capture-linux` ve
`apps/desktop/src/main/capture/kayit-linux.test.ts`); buradakiler **elle** denenmesi gerekenler.

Kurulum: `sudo apt install ./Zeyvola-<sürüm>-linux-x64.deb` (Debian/Ubuntu/Pardus).
Kaldırma: `sudo apt remove zeyvola` — projeler `~/Videos/Zeyvola Projeleri` altında kalır.

## 1. Açılış ve arayüz

| # | Deneme | Beklenen |
|---|---|---|
| 1.1 | Uygulamayı aç (`zeyvola` ya da uygulama menüsünden) | Pencere açılır, başlık çubuğu düğmeleri sağda, arayüz Türkçe/İngilizce (sistem diline göre) |
| 1.2 | Ayarlar → Kayıt yöntemleri | "Yerel kayıt (Linux)" **Kullanılabilir**; üç yetenek de işaretli |
| 1.3 | Ayarlar → Lisanslar | Lisanslar ekranı açılır, Chromium bildirimleri görünür |
| 1.4 | Pencere küçült/büyüt/kapat | Hepsi çalışır; tepsi simgesi yok (kayıt sırasında belirir) |

## 2. X11 oturumu (`XDG_SESSION_TYPE=x11`)

| # | Deneme | Beklenen |
|---|---|---|
| 2.1 | Yeni kayıt → tüm ekran → 3 sn geri sayım | HUD görünür, kayıt başlar, süre tray'de |
| 2.2 | Kayıt sırasında birkaç yere tıkla, kaydır | Düzenleyicide **otomatik zoom önerileri** görünür |
| 2.3 | Kaydı bitir | Proje açılır: `screen.webm` **imleçsiz**, `mic.webm`, `system.webm` ayrı izler |
| 2.4 | Sistem sesi açıkken müzik çal | `system.webm` müziği içerir, mikrofon sesi ayrı |
| 2.5 | Kamera açık kayıt | `camera.webm` ekrandan ayrı; kamerayı sonradan taşı/boyutlandır |
| 2.6 | Kayıt sırasında pencere kaydı (`--window`) | Kayıt pencerenin alanına kırpılır; pencere hareket ederse izler | 
| 2.7 | Bölge kaydı (16:9 kilitli) | Kayıt tüm ekran, düzenleyicide bölge kırpılmış |
| 2.8 | Duraklat/devam, işaret koy | Süre duraklamada durur, işaretler zaman çizelgesinde |
| 2.9 | Dışa aktar → WebM | Dosya oynar (mpv/VLC/Chrome), gizleme efektleri uygulanmış |
| 2.10 | Dışa aktar → MP4 | VA-API varsa MP4; yoksa uygulama **önceden** WebM'e düşeceğini söyler |

## 3. Wayland oturumu (`XDG_SESSION_TYPE=wayland`)

| # | Deneme | Beklenen |
|---|---|---|
| 3.1 | Yeni kayıt ekranı | Kaynak listesinde **"Tüm ekran"** görünür (portal önceden sorulmaz) |
| 3.2 | Kayıt yöntemi | "Yerel" **devre dışı**, "Otomatik" seçili; açıklama Wayland'ı anlatır |
| 3.3 | Yeni kayıt → Kaydı başlat | Masaüstünün **paylaşım penceresi** açılır; ekranı seç ve Paylaş |
| 3.4 | Onaydan sonra | Kayıt başlar, süre ilerler, dosya oluşur |
| 3.5 | Biten kayıt | Ekran izi vardır; **imleç videonun içindedir** (ayrı iz yok, otomatik zoom yok) |
| 3.6 | Onayı **reddet** | Kayıt başlamaz; uygulama hata gösterir, çökmez |
| 3.7 | Pencere kaydı | Paylaşım penceresinde pencere seçilebilir; kayıt o pencereyi alır |
| 3.8 | Sistem sesi | Kaydedilmez (tarayıcı yolu); anahtar devre dışı ve nedeni yazılı |

> Wayland'de "yerel" yardımcının kullanılmaması bilinçlidir: portal diyaloğu bir pencereye bağlı
> olmak zorundadır, ayrı süreçte pencere yoktur. Ayrıntı: `README.md` Linux bölümü.

## 4. Çökme ve kurtarma

| # | Deneme | Beklenen |
|---|---|---|
| 4.1 | Kayıt sırasında `pkill -9 zeyvola-kayit` | Uygulama kaydı kurtarır: "Kayıt yarıda kaldı; X sn kurtarıldı" |
| 4.2 | Kayıt sırasında uygulamayı `kill -9` ile öldür, yeniden aç | Kayıt projesine geri eklenir |
| 4.3 | Diski neredeyse doldur (1,5 GB altı) | Kayıt güvenli şekilde durur, dosyalar bozulmaz |

## 5. İçe aktarılan video sesi

| # | Deneme | Beklenen |
|---|---|---|
| 5.1 | Telefonla çekilmiş bir MP4 ekle | Ses izi eklenir (`sound.webm`), oynatınca duyulur |
| 5.2 | `gstreamer1.0-libav` kaldırılmış sistemde aynı MP4 | Uyarı: "ses okunamadı", video sessiz eklenir — sessizce kaybolmaz |

## 6. Paketleme

| # | Deneme | Beklenen |
|---|---|---|
| 6.1 | `dpkg -L zeyvola` | `/usr/bin/zeyvola`, `/usr/lib/zeyvola/*`, `.desktop`, ikonlar |
| 6.2 | `ls -l /usr/lib/zeyvola/chrome-sandbox` | `-rwsr-xr-x` (setuid) |
| 6.3 | `node apps/desktop/scripts/package.mjs --platform linux --arch x64` | `.deb` + `codec-audit-linux-x64.json`, `SONUÇ: uygun` |
| 6.4 | Kullanıcı ad alanları kapalı sistemde açılış | Uygulama `--no-sandbox` olmadan açılmazsa README'deki not geçerli |

## Bilinen sınırlar (bu sürümde)

- Wayland'de imleç videonun içindedir; ayrı imleç izi ve otomatik zoom yoktur (portal diyaloğu
  uygulamanın kendi penceresinden açılır, ayrı yardımcı süreçten açılamaz).
- Wayland'de global kısayol (`Alt+Shift+S`) çalışmaz; kayıt sırasında **tray menüsü** kullanılır.
- MP4 yalnız VA-API H.264 kodlayıcısı varsa; yoksa WebM.
- Duraklatma şu an yalnız saati durdurur (kareler akmaya devam eder, süre doğru kalır).
