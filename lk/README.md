# selene LK eklentisi — LittleSpammyMailman

Redmi 10 2022 (`selene`, MT6768) için **kaeru** LK (Little Kernel / bootloader)
eklentisi. Bu dizin, kernel ağacının bir parçası **değildir** — ayrı derlenir ve
`lk_a` bölümüne flaşlanır.

## ⚠️ LİSANS — kernel'den FARKLI

| | |
|---|---|
| kernel ağacı | **GPL-2.0** |
| bu dizin | **AGPL-3.0-or-later** |

Taban [kaeru](https://github.com/R0rt1z2/kaeru) (KAERU Labs S.L. /
Roger Ortiz), vrdons fork'u. Telif ve lisans satırları dosyaların içinde
korunmuştur. Ayrı bir program olduğu ve kernel'e linklenmediği için aynı depoda
durması sorun değildir, ama **bu dizindeki kodu kernel'e kopyalarken lisans
farkına dikkat.**

## Dosyalar

| dosya | ne |
|---|---|
| `board-selene.c` | asıl eklenti — auto-fastboot, OC anahtarı, log okuyucular |
| `board-selene-vrdons.c` | vrdons'un özgün selene board dosyası (referans) |
| `selene_defconfig` | LK yapılandırması (~26 adres, tersine mühendislikle çıkarıldı) |
| `reset-bootcount.sh` | boot sayacını cihazdan sıfırlar |
| `NOTES.md` | çalışma günlüğü — tersine mühendislik bulguları, tuğla olayı ve kurtarma |

## Ne yapıyor

### 1. auto-fastboot
N ardışık başarısız boot'tan sonra cihazı otomatik olarak fastboot'a düşürür.
Bootloop'ta kalan bir cihazı kurtarmak için tuş kombinasyonuna gerek kalmaz.

Sayaç yalnızca **NORMAL/ALARM** boot'ları sayar — şarj ekranı
(`POWEROFF_CHARGING`) ve recovery denemeleri sayılmaz. (Önceden sayılıyordu ve
sayaç bootloop olmadan doluyordu.)

### 2. CPU overclock anahtarı
```
fastboot oem cpuoverclock on     # ekranda kirmizi uyari + VOL UP onay
fastboot oem cpuoverclock off
```

**Neden yalnızca LK'de:** OC bayrağı userspace'ten, root ile bile
açılamamalı. Kernel tarafında `selene_oc_enabled` `__ro_after_init`
(`CONFIG_STRICT_KERNEL_RWX=y`), yani init bittikten sonra sayfa salt-okunur
olur. Ölçüldü: sembol `0xffffff80117e5cf0`, ro_after_init aralığı
`0xffffff80117e0f28`–`0xffffff80117e64bd` → içinde.

**⚠️ KOMUT ADI TUZAĞI:** bu komut önce `oem unlock cpuclock` idi. MTK LK'de
`oem lock` / `oem unlock` **öneki** komutu ele geçiriyor ve 2026-09-02'de
cihazdaki tüm veri bu yüzden silindi. Yeni bir `oem` komutu eklemeden önce stok
LK'nin dizelerini tara.

**Bootloop güvenlik ağı:** LK bootloop tespit ederse OC'yi **kendiliğinden
kapatır** ve ekrana yazar. OC'li kernel açılmazsa cihaz kilitlenmez.

### 3. LK → kernel kanalı: ayrılmış DRAM kelimesi
Cmdline yolu bu cihazda **kullanılamadı** — LK'nin cmdline tamponuna ekleme
yapmak, kernel'in tamamen yok saydığı zararsız bir token bile olsa, kernel
paniği üretiyordu (ölçüldü: `exp_type 0x2`, LK çökmesi değil).

Bunun yerine LK tek bir 64-bit sihirli değer yazıyor:

```
adres : 0x4D0E0000        (ramoops oyugunun son 64 KB'i, kernel'e "ayrilmis"
                           diye bildiriliyor; bu cihazda pstore hic kurulmadigi
                           icin oraya baska kimse dokunmuyor)
deger : "SELE" "OC01"     (0x53454C45 0x4F433031)
```

Kernel `core_initcall`'da (seviye 1) okuyup **temizler**; MTK cpufreq sürücüsü
`module_init` (6) ve `late_initcall` (7) seviyelerinde başladığı için OPP
tablosu seçilmeden önce okunduğu garanti.

Hata modu **iyi huylu**: çöp okunursa sihirli değer tutmaz, OC kapalı sayılır ve
cihaz stok hızlarda açılır. Temizlendiği için LK bir sonraki boot'ta yeniden
yazmazsa bayrak kendiliğinden düşer.

### 4. Log okuyucular
Boot etmeyen bir kernel'in izini fastboot üzerinden dökmek için —
`ramoops` / `mboot_params` / SRAM. `selene`'de ramoops ölü olduğu için bu yol
teşhisin büyük kısmını taşıdı.

## Derleme

kaeru ağacına board dosyası olarak eklenir; `selene_defconfig` LK adreslerini
sağlar. Çıktı `lk_a` bölümüne yazılır.

## Kurtarma

LK bozulursa cihaz preloader/BROM üzerinden kurtarılır:
```
mtk.bat w lk_a lk_a-device.bin      # stok LK, md5 0ed197a3eda3e64bee8c3f0357ffa0b4
```
Ayrıntı: `NOTES.md` → "TUĞLA OLAYI ve KURTARMA".
