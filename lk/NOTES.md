# ✅ YENİ LK BUILD: bootloop ekran metni eklendi (2026-08-23) — FLAŞ BEKLİYOR

**İmaj:** `lk-bootlooptext-flash.bin` — md5 `da960bccf3f861baae486dab00351bb1`,
2 MB (2.097.152 bayt). `lk-memdump2-flash.bin`'in (**8bc49bab**, hâlâ flaşlanmadı)
yerini ALIR — onun tüm içeriği (cpuinfo, rdmem, MMU eşleme) + bootloop ekran
metni var. Tek flaş ikisini de kapsar.

## Ne eklendi
`board-selene.c` → `show_bootloop_screen()`: auto-fastboot eşiği aşıldığında
(5 başarısız boot) ekrana (`video_printf`) İngilizce bilgilendirme basar:
BOOTLOOP DETECTED başlığı, olası nedenler (kernel panic / WDT / bozuk boot_a),
işe yarar komutlar (oem ramoops / rrlog / sram), sayaç sıfırlandı notu.
Konsola/loga (`printf` → expdb) `[autofb]` önekli özet gider. Yalnızca eşik
aşılma dalında çalışır; normal boot yoluna dokunmaz.

## Doğrulama zinciri
- Derleme: `make CROSS_COMPILE=arm-none-eabi-` (exit=0), payload **14.264** +
  stage1 **852** bayt; hedef `0x4C560D50`, tavana (~20.9 KB) ~5.8 KB pay —
  memdump2 ile aynı güvenli yerleşim.
- `patch.py` çıktısı **1.607.608 bayt** (kompakt) → 2 MB'a SIFIRLA padlendi.
- `lk-memdump2-flash.bin` ile karşılaştırma: **yalnızca 4 fark bölgesi, hepsi
  payload alanında** (0x184e04–0x1887a6) — geri kalan bayt bayt aynı. Kuyruktaki
  489.544 bayt memdump2'de de sıfır (pad).
- Metinler imajda doğrulandı (`KAERU AUTO-FASTBOOT` @0x187b45 vb.);
  `liblk` ile geri açılış: partitionlar `['lk', 'lk_main_dtb', 'kaeru']`.

## ⚠️ Bu oturumda öğrenilen tuzak: `/tmp` + truncate
İlk enjeksiyon çıktısı `/tmp`'ye yazıldı; WSL oturumu kapanınca `/tmp` silindi,
sonraki adımdaki `truncate -s 2M` **2 MB'lık tamamen SIFIR dosya** üretti
(md5 `b2d1236c…` — GEÇERSİZ, asla flaşlanmamalı). Ders: enjeksiyon çıktısını
`/mnt/c`'ye yaz, pad'i `truncate` ile değil sıfır-ekleme ile yap, md5'i her
turda bir öncekinden farklı olduğunu **içerik doğrulamasıyla** birlikte kontrol et.

## ➡️ Test sırası (cihaz bağlanınca)
1. `fastboot flash lk_a lk-bootlooptext-flash.bin` (veya mtkclient) → geri
   okuyup md5 `da960bcc…` karşılaştır
2. Reboot → dieD normal açılmalı; `fastboot oem cpuinfo` (yeni, MMU tespiti)
3. Bootloop testi: bilinen bootloop imaj yaz (veya 5 kez reboot — sayaç her
   LK geçişinde artar) → fastboot'a düşünce ekranda BOOTLOOP DETECTED metni
4. Kurtarma: `mtk.py w lk_a lk-analysis/lk_a-device.bin` (`--preloader`'sız)

---

# ⛔ TUĞLA OLAYI ve KURTARMA (2026-08-22 gece) — teşhis LK'sı cihazı boot edemez hale getirdi

`board-selene.c`'ye kernel-devri teşhis loglaması eklenirken cihaz **logo bile
gelmeden siyah ekran + loop** durumuna düştü; fastboot'a girilse bile ~4 saniyede
kendini resetliyordu. **Kurtarıldı** (aşağıda), cihaz stok LK + dieD ile sağlıklı.

## Ne yapıldı (kronoloji)
Üç LK derlendi ve arka arkaya `lk_a`'ya yazıldı:
| # | değişiklik | md5 | sonuç |
|---|---|---|---|
| 1 | teşhis `cmdline_pre_process` kancasında | `e69ef1ac…` | boot etti, çıktı YOK |
| 2 | kanca `board_early_init`'e taşındı (koşulsuz) | `cddaa4a5…` | boot etti, kanca kuruldu (`Found cmdline_pre_process at 0x4C424D7A`) ama çıktı YOK |
| 3 | teşhis `board_late_init`'e taşındı | `dfaea18d…` | bir kez boot etti, çıktı YOK; **sonra loop** |

## Kök neden — kesin değil, iki aday
1. **`board_late_init` içindeki `partition_read` çağrıları.** Teşhis `boot_a` ve
   `boot_b`'nin 2 KB başlığını okuyor. LK'nın watchdog'una takılıyor olabilir.
2. ~~`static` değişken / `.bss` sıfırlanmıyor~~ — **BU HİPOTEZ ÇÜRÜDÜ.**
   `main/start.S` `__bss_start`–`__bss_end` arasını `memset` ile sıfırlıyor
   (kaynaktan doğrulandı). `.bss` 2561 bayt, `0x4C564050`–`0x4C564A51` arasında,
   LK'nın cmdline tamponundan (`0x4C565F1E`) uzakta — çakışma da yok.
   → `static int done` her boot'ta 0'dı, yani teşhis gövdesi çalışmalıydı.
   **Teşhisin hiçbir çıktı vermemesi hâlâ AÇIKLANAMADI.**

`#3` bir kez sorunsuz açıldığı için arıza deterministik değil; auto-fastboot
sayacı eşiğe dayanmışken farklı bir dala girmiş olabilir.

## ✅ KURTARMA — preloader modu üzerinden, BROM'a gerek kalmadan
`fastboot` penceresi (~4 sn) yakalanamadı. Kurtarma mtkclient ile yapıldı:
```powershell
cd C:\Users\Eren\selene-kernel-project\mtkclient
.\.venv-win\Scripts\python.exe mtk.py w lk_a "...\lk-analysis\lk_a-device.bin"
```
**`--preloader` VERİLMEDİ** — cihaz `Preloader - Detected regular mode !` diyordu,
yani preloader modundaydı ve DRAM zaten kuruluydu. `--preloader` verildiğinde
"Sending emi data"da sonsuza kadar asılı kaldı. Detay: [[tool-mtkclient-windows]].

Sonuç: `Wrote ... to sector 1015808 with sector count 4096`, cihaz açıldı.
Doğrulanan son durum: `lk_a=0ed197a3…` (stok), `boot_a`/`dtbo_a` = dieD yedekleri,
`boot_completed=1`, `4.14.356-dieD`.

## Dersler
- **LK yazarken geri okuma md5'i YETMEZ.** O yalnızca "yazılan = gönderilen" der;
  "gönderilen = az önce derlediğim" demez. Bu turda enjeksiyon bir kez sessizce
  başarısız oldu (`/tmp` temizlenmiş, stok LK dosyası kaybolmuştu), `cp` de
  başarısız olunca **eski imaj yeniden yazıldı** ve md5 yine eşleşti.
  → Her turda enjeksiyon çıktısını VE nihai dosyanın md5'inin bir öncekinden
  farklı olduğunu ayrıca doğrula.
- **KAERU payload'ında `static` kullanma** (.bss sıfırlanmıyor).
- **`partition_read`'i LK'nın geç aşamalarında çağırmak riskli.**
- Payload tavanı: hedef `0x4C560D50`, LK'nın cmdline tamponu `0x4C565F1E` →
  **~20.9 KB sınır**. Şu anki payload 12.9 KB.

## Şu anki durum
Cihazda **stok LK** var: auto-fastboot YOK, teşhis loglaması YOK.
`kaeru-autofastboot/lk-diag-flash.bin` (md5 `b39d3250…`) derlenmiş ama
**flaşlanmadı ve doğrulanmadı** — `.bss`/`partition_read` sorunları çözülmeden
flaşlanmamalı.

---

# ⛔ UYARI (2026-08-22 gece): bu LK, 4.19 kernel testlerini BOZUYOR

> ### ⚠️ 2026-08-22 SONRAKI DUZELTME: bu bolumun "KANITLANDI" iddiasi GECERSIZ
> Cikarim `fiq_step 0x47` kayit SAYISININ artmamasina dayaniyordu. Ama **expdb bir
> HALKA tampon** — yeni kayit eklenince eski kayitlar disari duser, yani sayinin
> sabit kalmasi "yeni panik yazilmadi" demek DEGIL. (Ham dizgi sayimi zaten dort
> dokumun dordunde de 13 veriyor.)
>
> "Konsol logu yok"un masum bir aciklamasi da var: KAERU LK ile cihaz 5 turda
> **fastboot'a park ediyor**, biz oradan 72 MB'lik `boot_a`+`dtbo_a` yaziyoruz ve
> bu transfer DRAM'deki ramoops bolgesini (`0x4d010000`) ezmis olabilir. Stok LK'da
> cihaz surekli loop'ta oldugu icin bu adim hic yasanmiyordu.
>
> → **KAERU LK sucsuz sayilmali.** Kesin ayrim icin `fiq_step` izleri gerekiyor:
> onlar SRAM'de tutulup expdb'ye (flash) gecdigi icin DRAK ezilmesinden etkilenmez.


Auto-fastboot mantığı çalışıyor (aşağıda, iki kez doğrulandı). **Ama bu LK
yerindeyken 4.19 kernel test sonuçları geçersiz.**

Kanıt: `flash-test-scp2.img` (`#17`) — 21 Ağustos'ta stok LK altında 221 KB
konsol logu bırakıp `fiq_step 0x47` ile paniklemişti. **Aynı imaj**, aynı stok
dtbo, aynı doğrulanmış prosedürle KAERU LK altında:
- yeni panik kaydı **YOK** (`0x47` sayısı x12 → x12)
- yeni **takılma** kaydı VAR (`fiq_step 0x0` x8 → x9)
- konsol logu **hiç yok**

Değişen tek değişken bootloader. dieD 4.14 etkilenmiyor (sorunsuz açılıyor),
o yüzden fark uzun süre gözden kaçtı — KAERU'nun uçtan uca testi de zaten
bootloop yapan `#17` ile yapılmıştı, "bootloop oldu" beklenen sonuç sanıldı.

**Mekanizma bilinmiyor.** Elenen: kernel cmdline (KAERU altında stok ile birebir
aynı, ramoops parametreleri dahil). Kalan adaylar: bellek rezervasyonu/yerleşimi
(payload LK'yı ~120 KB büyütüyor), imaj yükleme yolu, dtbo overlay, her boot'taki
env yazımı.

**Kernel testine dönmeden önce stok LK'yı geri yaz:**
`../lk-analysis/lk_a-device.bin` — md5 `0ed197a3eda3e64bee8c3f0357ffa0b4`
(bu LK: `1732f7645bde38ce614217c731fdd19e`)

---

# selene auto-fastboot (KAERU eklentisi) — durum

## HEDEF
selene LK'da N ardışık başarısız boot'tan sonra otomatik fastboot. Stok LK bunu
yapmıyor (retry mantığı sadece priority karşılaştırıyor — Ghidra ile kanıtlandı,
`FUN_4804e08c`). KAERU ile ekliyoruz.

## KESİNLEŞENLER
- **LK yamalanabilir** — KAERU lk.bin (masaüstü) selene'de BOOT ETTİ. Preloader
  LK payload imzasını zorlamıyor. `cert1` aynı, payload 120KB farklı, yine boot etti.
- KAERU = R0rt1z2/kaeru (github, klonlandı `/root/kaeru` WSL). Board hook'ları
  `SEARCH_PATTERN` ile (cihazdan bağımsız) → fire'ın `board-fire.c` pattern'leri
  selene'de çalışır.
- **Auto-fastboot kodu HAZIR:** `board-selene.c` (misc@0x20000'de sayaç, eşik=5,
  `set_bootmode(BOOTMODE_FASTBOOT)`) + `reset-bootcount.sh` (Android sıfırlama).

## KALAN TEK ENGEL: selene_defconfig (~26 LK adresi)
Public'te YOK (Telegram build). Çıkarılıyor. Referans: `configs/xiaomi/fire_defconfig`
(fire≈selene, ikisi de MT6768, taban 0x4C400000). Gereken alanlar (Kconfig):
APP_ADDRESS, BOOTMODE_ADDRESS, PLATFORM_INIT_ADDRESS+CALLER, INIT_STORAGE_ADDRESS+CALLER,
DPRINTF, VIDEO_PRINTF, MALLOC, FREE, PARTITION_READ, PARTITION_GET_SIZE_BY_NAME,
GET_ENV, SET_ENV, FASTBOOT_REGISTER/PUBLISH/FAIL/INFO/OKAY, LK_LOG_STORE,
MTK_DETECT_KEY, THREAD_CREATE/RESUME, RECOVERY_CMDLINE1/2, BOOTLOADER_SIZE.

### ÇIKARMA DURUMU
- **DOĞRULANDI:** `CONFIG_DPRINTF_ADDRESS = 0x4C43B3A8` (hem 38-aday hem reversing notu).
- **Ghidra projesi HAZIR:** `lk-analysis/lk-selene-proj` (stok LK payload,
  taban 0x4C400000, Thumb). Payload: `lk-analysis/lk_payload_selene.bin` (1445200 bayt).
- **38 aday adres** (KAERU lk.bin'de olup stokta olmayan 0x4C4-0x4C5 sabitleri)
  `lk-analysis/`de çıkarıldı — bunlar KAERU-selene'nin GERÇEK kullandığı adresler,
  config değerlerini içeriyor. Bunları fire alanlarıyla eşle.

### YÖNTEM (sonraki adım)
Her fonksiyonu Ghidra'da string-xref / pattern ile bul, VA'sını oku:
- dprintf ✓, video_printf (framebuffer'a basar, dprintf yakını)
- malloc/free (standart heap), partition_read/get_size (partition string xref)
- fastboot_register ("fastboot" registration xref)
- BOOTMODE_ADDRESS (global; boot mode select yazar)
- APP_ADDRESS (mt_boot app_descriptor), platform_init + caller
- En güvenilir çapraz kontrol: 38 aday + çalışan lk.bin stage1 sabitleri.

## BUILD (config hazır olunca)
1. `/root/kaeru`'ya `configs/xiaomi/selene_defconfig` + `board/xiaomi/board-selene.c`
   (bu dizindeki board-selene.c'yi kopyala, fire'ın SPOOF pattern'lerini de ekle).
2. `soc`/`Makefile`'da selene'yi tanıt (fire'ı örnek al).
3. Derle → `inject_payload` ile stok lk_a'ya göm → 2MB'a padle → fastboot flash lk_a.
4. Test: boot + `fastboot oem kaeru-version`. Sonra 5 kez bootloop simüle et → oto-fastboot.
   Kurtarma her zaman: `mtk.bat w lk_a lk_a-device.bin` (BROM, md5 0ed197a3…).

## KURTARMA AĞI
BROM + mtkclient KANITLANDI (sigtest tuğlasından kurtarıldı). Zadig WinUSB kurulu.
Yanlış adres = tuğla ama BROM her zaman kurtarır.

## ✅ 2026-08-22 — UÇTAN UCA DOĞRULANDI, ÇALIŞIYOR

**Kaynak:** `vrdons/kaeru` (GitHub, vrdons = selene phison kernel yapımcısı) —
`configs/xiaomi/selene_defconfig` (26 adres, THREAD_CREATE typo düzeltildi:
`0c4C4223C4`→`0x4C4223C4`) + `board/xiaomi/board-selene.c`. DPRINTF adresi
benim bağımsız RE çıkarımımla (`0x4C43B3A8`) birebir eşleşti.

**Kritik API değişikliği:** vrdons fork'unda `storage_part_*` YOK — LK'nın raw
partition yazma fonksiyonu expose edilmemiş (`partition_read` var, write yok).
Bunun yerine **`get_env`/`set_env`** kullanıldı — LK'nın kendi env/nvram
kalıcılık mekanizması, zaten çalışır durumda (`CONFIG_GET_ENV_ADDRESS`/
`SET_ENV_ADDRESS` stok LK'dan). Sayaç `kaeru.bootcount` env değişkeninde.

**Build:** `arm-none-eabi-gcc` (Arch paketi) + `arm-none-eabi-newlib`.
`Makefile`'da `KBUILD_AFLAGS`'a `-mcpu=cortex-a15 -mthumb` eklenmesi gerekti
(assembler'a CPU bayrakları gitmiyordu, `.S` dosyaları Thumb-2 komutlarını
reddediyordu). `make selene_defconfig && make -j$(nproc)` → `kaeru` + `stageone`.
Enjeksiyon: `utils/patch.py <defconfig> stock-lk.bin kaeru -l stageone -o out.bin`
(venv + `liblk`/`pyasn1`/`capstone` gerekli). Çıktı: `"Re-signed modified
partition 'lk' (wrap)"` — `CONFIG_CERT_BYPASS=y mode="wrap"` sayesinde.

**UÇTAN UCA TEST (gerçek bootloop, sıfır tuş basımı):**
1. `flash-test-scp2.img` (`#17`, bilinen bootloop: logo→panik→WDT reset) +
   stok dtbo yazıldı, `kaeru.bootcount` 0'a sıfırlandı, reboot.
2. **30 saniye içinde, HİÇ TUŞA DOKUNMADAN**, `fastboot devices` cihazı gösterdi.
3. `fastboot oem env get kaeru.bootcount` → **`0`** — bu sadece eşik-aşıldı
   dalında set ediliyor, yani tesadüf değil: cihaz gerçekten 5 turu (LK→kernel
   panik→WDT reset) otomatik saydı ve kendi kendine fastboot'a düştü.
4. dieD + stok dtbo geri yazıldı, normal açıldı (`boot_completed=1`,
   `4.14.356-dieD`), auto-fastboot LK hâlâ aktif.

**Sonuç:** Bundan sonra kernel testinde bootloop olursa Ses Kısma'ya GEREK YOK
— cihaz ~30 saniye içinde (5 döngü) kendiliğinden fastboot'a düşüyor.

**Dosyalar:** `lk-autofastboot-flash.bin` (2MB, md5 `1732f7645bde38ce614217c731fdd19e`)
— bu artık kalıcı LK, `lk_a`'da duruyor. Kurtarma hâlâ `mtk.bat w lk_a
lk_a-device.bin` (orijinal stok LK, sayaç yok).

**Kullanım notları:**
- Sayaç HER LK geçişinde artıyor (Android açılışı dahil) — normal reboot'lar da
  sayar. Eşik 5 olduğu için tek reboot sorun değil, ama sık reboot + bootloop
  karışırsa dikkat.
- Başarılı Android boot sayaç sıfırlamıyor (reset-bootcount.sh script'i artık
  YANLIŞ — misc yazma değil, `fastboot oem env set kaeru.bootcount 0` gerekir,
  ve bu fastboot'tan yapılır, adb'den değil). Normal kullanımda sorun değil
  çünkü eşik yüksek ve reboot nadiren 5 kez üst üste olur.

---

# 2026-09-02 — OVERCLOCK MODU (LK + kernel) + LK renk/bug düzeltmeleri

**Çıktılar:** `selene-kernel-project/oc-build/`
- `lk-oc-flash.bin` — 2.097.152 bayt, md5 `a28cb1d8821d0148df7d82248d5ed29d`
- `boot-k419-oc.img` — 42.952.704 bayt, md5 `7b2acaeebc99ef5ceebdeef03e4e4462`

**İKİSİ DE HENÜZ FLAŞLANMADI.** Kurtarma: `mtk.bat w lk_a lk_a-device.bin`
(stok LK, md5 `0ed197a3eda3e64bee8c3f0357ffa0b4`, BROM üzerinden).

## Nasıl çalışıyor

`fastboot oem unlock cpuclock` → ekranda İngilizce uyarı → VOLUME UP kabul /
VOLUME DOWN iptal → `kaeru.oc=1` env'e yazılır. Sonraki boot'ta LK cmdline'a
`selene.oc=1` ekler; kernel bunu görürse PRO OPP tablosuna geçer.
Kapatma: `fastboot oem lock cpuclock`.

| | LL (A55) | L (A75) | LL voltaj | L voltaj |
|---|---|---|---|---|
| G75 (stok) | 1800 MHz | 2000 MHz | 1006,25 mV | 1087,50 mV |
| PRO (OC) | 2000 MHz | 2202 MHz | 1081,25 mV | 1118,75 mV |

Tepe voltaj 111875 < `MAX_VPROC_VOLT` (112000) → kırpılmıyor, PMIC adımına
(625 µV) tam oturuyor. `set_cur_volt_sram_cpu` bu platformda `#if 0` —
VSRAM donanımda otomatik takip ediyor, yazılım kısıtı yok.

## Kernel tarafı (`/root/k419`)

- `drivers/cpufreq/cpufreq.c` — `selene_oc_enabled` **`__ro_after_init`**,
  `__setup("selene.oc=")`. `CONFIG_STRICT_KERNEL_RWX=y` olduğu için init
  sonrası sayfa salt-okunur. **Ölçüldü:** sembol `0xffffff80117e5cf0`,
  ro_after_init aralığı `0xffffff80117e0f28`–`0xffffff80117e64bd` → içinde.
  Yani root'lu userspace de, kernel belleğine yazan modül de çeviremez.
- `__cpufreq_driver_target()` — OC açıkken hedef `cpuinfo.max_freq`e
  sabitleniyor, **bilerek `clamp_val` SONRASINDA**: termal soğutma cihazları
  ve userspace `policy->max`ı freq_qos üzerinden düşürse bile geri alınır.
- `store_scaling_governor` / `scaling_min_freq` / `scaling_max_freq` → `-EPERM`
- `cpufreq_init_policy()` — kayıtlı governor ne olursa olsun `performance`
- `mach/mt6768/mtk_cpufreq_platform.c` — `_mt_cpufreq_get_cpu_level()` OC'de
  `CPU_LEVEL_2`/`_5` (PRO) döndürüyor. Satıcının kendi ptp kuralı korundu.
  Tek nokta yeterli: frekans/voltaj, PLL method ve PV tabloları aynı indeksten.

**Acil kapanma DOKUNULMADI:** `mtk_cooler_kshutdown` doğrudan
`machine_power_off()` çağırıyor, cpufreq yolunu hiç kullanmıyor. Yani frekans
kilidi throttling'i etkisiz bırakıyor ama termal kapanmayı olduğu gibi
bırakıyor. `CONFIG_MTK_PPM is not set` — ayrıca PPM kısıtlama yolu da yok.

## LK tarafı

**Uyarı ekranı:** tamamı BÜYÜK HARF olan 9 satır saf kırmızı (255,0,0).
Renk fonksiyonları tersine mühendislikle bulundu (kendi log format dizeleriyle
doğrulandı, tahmin yok):

| | adres |
|---|---|
| `video_set_fg_color(r,g,b)` | `0x4C43AD6C` |
| `video_set_bg_color(r,g,b)` | `0x4C43AE78` |
| fg / bg / fg^bg global | `0x4C574D70` / `D58` / `D94` |
| ekran bilgi işaretçisi | `0x4C574D90` |

Türetilmiş `fg^bg` global'i de saklanıp geri yükleniyor — yalnız fg geri
yazılırsa metin çizimi bozuluyor. Renk fonksiyonu ekran bilgi işaretçisini
dereference ettiği için NULL kontrolü zorunlu, kondu.

**Düzeltilen gerçek kusurlar:**
1. `VOLUME_UP`/`VOLUME_DOWN` hiç tanımlı değildi — dosya daha önce hiç
   derlenmemiş. Selene değerleri kernel DTS'inden: `volup=17`, `voldown=0`
   (`kpd-hw-init-map[0]=114`, `kpd-hw-rstkey=17`; `kpd-hw-dl-key0/1/2 =
   17,0,8` ile çapraz doğrulandı).
2. Şarj ekranı (`POWEROFF_CHARGING`) ve recovery boot denemesi sayılıyordu →
   sayaç bootloop olmadan doluyordu. Artık yalnız NORMAL/ALARM sayılıyor.
3. Env her boot'ta yeniden yazılıyordu → değişmediyse hiç yazılmıyor.

## AÇIK KONULAR

- **`selene.oc=1` kernel'e ulaşıyor mu, DOĞRULANMADI.** `oc_apply()`
  `board_late_init`ten yazıyor; diğer KAERU board'ları cmdline'a
  `cmdline_pre_process` hook'uyla dokunuyor (`PATCH_CALL`). Selene'de o
  fonksiyonun adresi henüz bulunmadı (cmdline tamponlarına sabit adresle değil,
  taban+offset ile erişiliyor; xref taraması boş döndü).
  **Risk yok:** bayrak ulaşmazsa cihaz stok hızlarda açılır.
  **Doğrulama:** OC açıkken reboot → `cat /proc/cmdline` → `selene.oc=1` var mı.
  LK logunda `[oc] cmdline @0x... uzunluk=N` satırı tamponun o an dolu olup
  olmadığını gösterir.
- `handle_recovery_boot()` selene'de derlenmiş ama **hiç çağrılmıyor** (hook
  yok) — recovery'de verifiedbootstate yaması bu cihazda çalışmıyor demek.
