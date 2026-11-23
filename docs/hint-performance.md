# İpucu modu gecikmesi

## Sorun

`Ctrl+Alt+F`, erişilebilirlik hazırlığını ve altı tam AT-SPI taramasını arayüz iş parçacığında yapıyordu. Tarama aralarındaki beş 80 ms beklemeye D-Bus çağrıları ve ağaç dolaşımı da ekleniyordu. Yalnızca son taramanın hedefleri gösteriliyordu. Grid modu AT-SPI taraması yapmadığı için bu gecikmeyi yaşamıyordu.

## Uygulanan yöntem

- Erişilebilirlik hazırlığı, yoklamalar ve tam tarama aynı yardımcı süreçte çalışır. Yardımcı süreç boşta yeniden kullanılır. Libatspi bazı D-Bus bağlantılarını sürecin varsayılan GLib bağlamına bağladığı için süreç ayrımı, bu bağlantıları ve bekleyen çağrıları arayüzden yalıtır.
- Erişilebilirlik hazırlığından sonra 0, 80, 160, 240 ve 320 ms civarında en fazla beş sığ yoklama planlanır. Bir sağlayıcı yavaş yanıt verdiğinde süresi geçmiş yoklamalar atlanır. Yoklamalar eşleşen pencerelerin erişilebilirlik ağaçlarını etkinleştirir ve köklerin yakınındaki öğeleri kontrol eder. Yaklaşık 400 ms sonra güncel KWin pencereleriyle tek tam tarama başlar. Kökler erken görünse bile geç yüklenen Chromium ve Electron öğeleri için bu süre korunur. İpuçları yalnızca son taramadan üretilir.
- Başarılı taramadan sonraki 10 saniye içinde pencere düzeni ve odak aynıysa tam tarama hemen başlar. Önceki taramadan yalnızca pencere başına hedef sayısı tutulur. Tarama başarısız olursa veya herhangi bir pencerede hedef sayısı düşerse normal hazırlık ve tam tarama tekrarlanır. Her durumda gösterilen hedeflerin koordinatları son taramadan gelir.
- Yeni bir ipucu isteği, grid moduna geçiş veya KWin odak, pencere ya da ekran düzeni değişikliği bekleyen isteği iptal eder. Fare imlecinin tek başına hareket etmesi iptal nedeni sayılmaz. Sonuç gösterilmeden önce düzen tekrar doğrulanır ve tıklama öncesi hedefin geçerliliği yeniden kontrol edilir.

## Ölçüm ve doğrulama

```sh
QT_LOGGING_RULES='vimwhere.hints.performance.debug=true' ./build/vimwhere
```

Portal iznini tamamladıktan sonra kayıtları `hint` numarasına göre grupla. `elapsed_ms` ile erişilebilirlik hazırlığı, yoklamalar, son tarama, overlay isteği ve ilk kare ayrı ayrı ölçülebilir. `phase=first-frame`, kısayoldan odaklı ekrandaki ilk overlay karesine kadar geçen süreyi gösterir. Karşılaştırma için aynı pencere ve ekran düzeninde ilk ve tekrarlanan denemeleri ayrı kaydet. İpucu kapsamını da karşılaştır. [Masaüstü test adımları](manual-testing.md#hint-latency)

İzole qmake derlemeleri, tarayıcı ağacı ve yeniden deneme testleri ile özel D-Bus oturumunda gerçek bir GTK düğmesini bulan yardımcı süreç denemesi geçti. Kullanıcının ilk izlenimi, ipuçlarının biraz daha hızlı açıldığı yönünde. Canlı Plasma oturumunda karşılaştırmalı süre ölçülmedi.
