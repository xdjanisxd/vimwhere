# Vimwhere yol haritası

Bu sıra, mevcut KDE Wayland, Qt ve AT-SPI mimarisini koruyarak en büyük doğruluk belirsizliklerini önce ele alır. Canlı masaüstü davranışı henüz uçtan uca doğrulanmadığı için performans ve uyumluluk iddiaları ölçümle teyit edilmelidir.

## 1. Koordinat doğruluğu ve KDE masaüstü doğrulaması

**Hedef:** AT-SPI hedef koordinatlarının KWin ve portal tıklama koordinatlarıyla gerçek KDE Wayland oturumunda uyuştuğunu doğrulamak.

**Mevcut sorun:** Kod koordinatları mantıksal ekran koordinatlarında eşliyor; farklı ölçekli monitör, panel/örtüşme ve portal stream konumları README'de doğrulanmamış olarak belirtiliyor.

**Yaklaşım:** `docs/manual-testing.md` senaryolarını gerçek KDE Wayland oturumunda çalıştır. Tek ve çok ekran, negatif ekran başlangıç koordinatı, farklı ölçek, pencere taşıma, panel örtüşmesi ve portal izin/oturum akışında hint ile grid tıklamalarını kaydet. Hata bulunan senaryolar için tekrarlanabilir geometri testleri ekle.

**Tamamlanma ölçütü:** Her senaryonun sonucu, Plasma/Qt/monitör ölçek bilgisiyle birlikte kaydedilmiş; hedeflenen noktada yanlış tıklama olmadan hint ve grid akışları geçmiştir. Başarısız senaryolar için düzeltme ve regresyon testi vardır.

**Risk:** Uygun donanım ve portal izinleri olmadan çoklu monitör ve ölçek davranışları doğrulanamaz. Başarı varsayılmamalı, ölçüm sonucu raporlanmalıdır.

## 2. Hint taramasında erken sonuç ve ölçüm

**Hedef:** Erişilebilirlik ağacı hazır olduğunda beklemeyi azaltırken hedef listesinin tutarlı kalmasını sağlamak.

**Mevcut durum:** Hint modu beş hafif yoklamadan sonra tek bir hedef taraması yapıyor. Ölçüm günlükleri tarama süresini, ziyaret edilen düğümleri ve limit nedeniyle kesilmeleri gösteriyor.

**Yaklaşım:** Tarama süresi, ziyaret edilen düğüm, bulunan hedef ve bütçe/derinlik kesilmesini ölç. Erken tamamlanma koşulunu yalnızca aynı snapshot içindeki hedeflerin kararlı olduğu kanıtlanırsa pilotla; zaman aşımı ve mevcut son tarama fallback'ini koru. Büyük ve geç yüklenen ağaçlarda ölçümleri karşılaştır.

**Tamamlanma ölçütü:** Temsilî Chromium sayfalarında kısayol ile overlay arasındaki süre ve hedef tamlığı kaydedilmiş; erken sonuç koşulu hedef kaçırmıyor veya mevcut son tarama davranışına güvenli şekilde geri dönüyor. Limit kesilmeleri anlaşılır hata/diagnostic olarak raporlanıyor.

**Risk:** Daha erken sonuç daha az gecikme sağlayabilir, ancak geç yüklenen düğmeleri kaçırabilir. Gerçek zaman ve kapsama kazanımı ölçülmeden performans artışı iddia edilmemelidir.

## 3. AT-SPI doğrudan etkinleştirmesi ve portal yolu için küçük pilot

**Hedef:** Mevcut erişilebilirlik ve tıklama entegrasyonlarının oturumlar ve uygulamalar arasındaki davranışını dar kapsamlı şekilde değerlendirmek.

**Mevcut sorun:** Chromium/Electron erişilebilirlik ağacının etkinleştirilmesi özel bir AT-SPI cache/attribute adımına dayanıyor. Portal oturumu ve izin davranışının yeniden kullanımı da canlı ortamda doğrulanmamış.

**Yaklaşım:** Bir Chromium sürümü ve sınırlı sayıda örnek sayfada mevcut AT-SPI etkinleştirmesini test et. Ayrı pilotta portal hazırlama, izin reddi, izin verme, yeniden kullanım ve tıklama sonucunu kaydet. Değişiklik öncesi/sonrası hedef sayısı, süre ve hata durumlarını karşılaştır.

**Tamamlanma ölçütü:** Pilot sonuçları tekrarlanabilir ve mevcut akışla karşılaştırmalı olarak belgelenmiş; doğrudan etkinleştirme ya da portal değişikliği ancak kapsama veya hata oranına kanıtlanmış katkı sağlıyorsa sonraki uygulama işine alınmış.

**Risk:** AT-SPI sağlayıcılarının davranışı uygulama ve sürüme göre değişebilir; tek tarayıcıdaki sonuç genellenemez. Portal izin diyaloğu ve oturum yaşam döngüsü masaüstü ortamına bağlıdır.

Grid ve KWin entegrasyonunda daha geniş değişiklikler, bu üç başlığın sonuçlarına göre sonraki değerlendirme olarak ele alınabilir.
