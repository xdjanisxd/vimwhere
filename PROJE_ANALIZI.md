# Vimwhere proje analizi

## Genel bakış

Vimwhere, Chromium eklentisi değil; KDE Plasma/Wayland masaüstünde çalışan yerel bir C++ uygulamasıdır. Chromium tabanlı tarayıcı pencerelerindeki erişilebilir öğeleri AT-SPI üzerinden tarayarak harf ipuçları gösterir. Ayrıca ekranı 1–9 ile seçip 3×3 hücrelerle daraltarak fare tıklaması yapmayı sağlar. Global kısayollar KDE GlobalAccel ile yönetilir, overlay'ler Qt Quick ve LayerShellQt ile çizilir, tıklama ise masaüstü portalının RemoteDesktop API'si üzerinden gönderilir.

Başlangıç noktası [src/main.cpp](src/main.cpp#L35), uygulama akışının merkezi [src/AppController.cpp](src/AppController.cpp#L19) dosyasıdır.

## Teknolojiler ve derleme

- **C++20 ve Qt 6** kullanılır. Qt Core, Gui, Quick, QML ve D-Bus modülleri projeye bağlıdır.
- **KDE Frameworks 6** tarafında KF6GlobalAccel global kısayolları, LayerShellQt masaüstü overlay katmanını sağlar.
- **AT-SPI 2** erişilebilirlik ağacından arayüz öğelerini okumak için kullanılır.
- **KWin JavaScript eklentisi** pencere ve ekran geometrisini uygulamaya aktarır.
- **XDG Desktop Portal RemoteDesktop/ScreenCast** kullanıcının izniyle imleç hareketi ve sol tıklama sağlar.
- İki derleme tanımı bulunur: [vimwhere.pro](vimwhere.pro) ve [CMakeLists.txt](CMakeLists.txt). README, Qt 6.5+, qmake6 ve KDE/AT-SPI geliştirme bağımlılıklarını listeler. Önerilen derleme qmake6 ile `build` dizininde yapılır.

## Dosya haritası

| Dosya veya dizin | Görevi |
|---|---|
| `src/main.cpp` | Qt uygulamasını başlatır, `--help` ve `--diagnose` seçeneklerini işler, `AppController`'ı çalıştırır. |
| `src/AppController.*` | Kısayolları bağlar, hint/grid modlarını ve klavye girişini yönetir, overlay'leri günceller ve tıklamayı başlatır. |
| `src/TargetScanner.*` | AT-SPI ağacında işlem yapılabilir hedefleri tarar, görünürlük/duyarlılık ve erişilebilir rol filtrelerini uygular. |
| `src/HintScanRetry.*` | Erişilebilirlik ağacı yüklenirken gecikmeli tekrar tarama akışını yürütür. |
| `src/AccessibilityStatus.*` | AT-SPI erişilebilirlik servisinin oturum durumunu kontrol eder/etkinleştirir. |
| `src/KWinBridge.*`, `kwin/main.js` | D-Bus ile KWin tarafında geometri köprüsünü başlatır; pencere, ekran ve imleç snapshot'larını sağlar. |
| `src/PortalClicker.*` | Desktop portal oturumu/izin akışını ve imleç hareketi ile sol tıklama çağrılarını yürütür. |
| `src/OverlayView.*`, `qml/Overlay.qml` | Ekran başına LayerShellQt penceresi ve Qt Quick arayüzünü oluşturur. |
| `src/NavigationMath.h` | Harf ipucu kodları, ekran koordinat dönüşümleri, 3×3 grid hücreleri ve ekran kesişimleri için yardımcı geometriyi içerir. |
| `src/WindowMatcher.h` | Erişilebilirlik pencerelerini KWin pencereleriyle eşleştirmeye yarayan başlık/kimlik yardımcılarını içerir. |
| `tests/` | Erişilebilirlik durumu, tarama tekrarı, hedef tarayıcı, portal oturumu, navigasyon geometrisi ve QML smoke testlerini içerir. |
| `docs/manual-testing.md` | Çalışan KDE Wayland oturumunda yapılacak elle kabul kontrollerini listeler. |

## Hint akışı · Ctrl+Alt+F

1. [AppController::start](src/AppController.cpp#L34) QSettings içindeki `shortcuts/hints` değerini okur. Varsayılanı `Ctrl+Alt+F`'tir ve KGlobalAccel'e kaydeder.
2. Kısayol `openHints()`'i çağırır. Portal hazır değilse izin hazırlığını başlatır ve kullanıcıdan izni tamamlayıp kısayolu yeniden basmasını bekler. Sonra AT-SPI'yi ve KWin geometri köprüsünü kontrol eder ([AppController.cpp](src/AppController.cpp#L81)).
3. Aktif pencere/ekran bilgisi alınır ve [HintScanRetry](src/HintScanRetry.cpp) erişilebilirlik ağacının yüklenmesi için aralıklı birden çok tarama yürütür. İstemci hedefleri [TargetScanner](src/TargetScanner.cpp) ile toplanır.
4. Tarayıcıdaki bağlantı, buton ve benzeri eyleme açık AT-SPI rolleri filtrelenir. Hedefe büyük harfli etiketler atanır ([NavigationMath.h](src/NavigationMath.h#L21)). Etiketler overlay'de gösterilir.
5. Kullanıcı etiketin harflerini yazınca eşleşen hedef seçilir; seçim öncesi kısa gecikme ve hedefin geçerliliğini yeniden kontrol etme adımı bulunur. Portal üzerinden tıklama gönderilir.

Tarayıcı içeriğinin ipuçlarında görünmesi için ilgili öğelerin erişilebilirlik ağacında bulunması gerekir. Tarayıcı eşleştirmesi süreç, başlık ve geometri bilgisine dayanır. Tarayıcı dışındaki tüm uygulamalar için kapsam garantisi verilmez.

## Grid akışı · Ctrl+Alt+G

1. Kısayol varsayılan olarak `Ctrl+Alt+G`'dir ve `openGrid()`'i çağırır.
2. Overlay önce 1–9 tuşlarıyla bir ekran seçtirir. Seçilen alan üzerinde tekrar 1–9, 3×3 hücreyi seçer. Bu işlem alan tek bir hücreye inene kadar tekrarlanabilir. Backspace önceki daraltmayı geri alır.
3. Enter yalnızca seçili alan tek bir ekranla kesiştiğinde çalışır. Seçilen bölgenin merkezine portal üzerinden sol tıklama yapılır. Ekranlar arasındaki boş alanda tıklama yolu yoktur.

Bu geometri [NavigationMath.h](src/NavigationMath.h#L36) içinde, klavye durum makinesi ve Enter davranışı [AppController.cpp](src/AppController.cpp#L265) içindedir.

## Linux, Wayland ve Chromium kapsamı

Uygulama özellikle yerel Wayland oturumu ister. `start()` Qt platformu `wayland` değilse uygulamayı başlatmaz ([AppController.cpp](src/AppController.cpp#L34)). Bu nedenle genel bir Chromium eklentisi gibi farklı işletim sistemlerinde tarayıcı API'leriyle çalışmaz. Chromium burada hedef arayüzü sağlayan uygulamalardan biridir; erişim AT-SPI üzerinden, sistem genelinde tıklama ise KDE ve XDG portal entegrasyonlarıyla sağlanır.

Portal ilk kullanımda kullanıcı izni ister. Hint taraması görünür, hassas ve eyleme açık AT-SPI öğelerini kapsar; tarama derinliği, düğüm ve çocuk sayısı için sınırlar vardır. Bir pencerenin kapalı/örtülü veya ekran dışı sayılması bazı hedeflerin atlanmasına yol açabilir. Çoklu monitörde en fazla dokuz ekran seçilebilir. Ekran düzeni mod açıkken değişirse mevcut işlem kapatılır.

## Doğrulama ve bilinen sınırlar

README'ye göre qmake derlemesi ve linkleme, navigasyon geometrisi kontrolleri, QML offscreen smoke testi, `qmllint`, `node --check` ve `--help` kontrolü geçmiş. `--diagnose` sandbox ortamında D-Bus ve AT-SPI kısıtları nedeniyle başarısız olmuş. Canlı KDE Wayland masaüstünde KWin köprüsü, portal izin/oturum davranışı ve gerçek fare tıklaması uçtan uca doğrulanmış değil. Panel örtüşmesi, AT-SPI ile ekran koordinatlarının hizası, ölçekleri farklı monitörler, portal oturumunun yeniden kullanımı, odak geri yükleme ve KWin yeniden başlatma sonrası toparlanma elle doğrulama bekliyor. Ayrıntılı kontrol listesi [docs/manual-testing.md](docs/manual-testing.md) dosyasındadır.

Bu rapor kaynak kodu ve README'de açıklanan mevcut kapsamı özetler. Canlı masaüstü testi gerçekleştirdiği iddiasında bulunmaz.
