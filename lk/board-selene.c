//
// SPDX-FileCopyrightText: 2026 Vrdons <vrdons@proton.me>
// SPDX-FileCopyrightText: 2026 R0rt1z2 <roger@r0rt1z2.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <board_ops.h>

//
// LittleSpammyMailman -- selene (Redmi 10 2022) LK eklentisi
//
// Taban: kaeru (AGPL-3.0-or-later), KAERU Labs S.L. / Roger Ortiz <r0rt1z2>,
// vrdons fork'u. Bu dosyadaki eklentiler projenin kendi kodu; kaeru'nun
// telif ve lisans satirlari yerinde birakilmistir.
//
// Eklenenler:
//   - auto-fastboot: N ardisik basarisiz boot sonrasi otomatik fastboot
//   - log okuyucular: ramoops / mboot_params / SRAM'i fastboot uzerinden dok
//
#define LSM_NAME    "LittleSpammyMailman"
#define LSM_VERSION "0.1"

#define KAERU_ENV_BOOTCOUNT  "kaeru.bootcount"
#define KAERU_ENV_OC         "kaeru.oc"

// Tus kodlari MTK kpd DONANIM keycode'lari (Linux keycode DEGIL). selene icin
// kernel DTS'inden birebir alindi -- tahmin degil:
//   arch/arm64/boot/dts/mediatek/selene/cust.dtsi
//     kpd-hw-init-map[0] = 114 (KEY_VOLUMEDOWN)  -> voldown = 0
//     kpd-hw-rstkey = 17, kpd-sw-rstkey = 115 (KEY_VOLUMEUP) -> volup = 17
//     kpd-hw-pwrkey = 8
//   Capraz kontrol: kpd-hw-dl-key0/1/2 = 17, 0, 8 (volup+voldown+power kombosu).
#define VOLUME_UP    17
#define VOLUME_DOWN  0
#define KEY_POWER_HW 8

// oc_enabled() asagida tanimli; auto_fastboot_check ondan once kullaniyor.
static bool oc_enabled(void);

// Kernel bu bayragi cmdline'dan okur. Android userspace cmdline'i
// calisirken degistiremez ve LK env'ine yazamaz -> OC yalnizca
// buradan, fastboot uzerinden acilip kapatilabilir.
#define OC_CMDLINE_TOKEN     " selene.oc=1"
#define BOOTLOOP_THRESHOLD   5

// N ardisik basarisiz boot sonrasi otomatik fastboot. Stok LK bunu yapmiyor
// (RE ile dogrulandi: slot secimi yalnizca priority karsilastiriyor). Sayac
// LK'nin kendi env deposunda; vrdons fork'unda raw partition yazma expose
// edilmemis, get_env/set_env ise zaten calisiyor.
// Env yazimi pahali (flash) ve her boot'ta yapiliyordu. Ayni degeri tekrar
// yazmanin faydasi yok; degismediyse hic dokunma. Bu ayni zamanda "payload
// her boot'ta env yaziyor" suphesini de (4.19 testlerini bozan aday) kaldirir.
static void set_env_degistiyse(char *anahtar, char *deger) {
    char *simdiki = get_env(anahtar);
    if (simdiki && strcmp(simdiki, deger) == 0)
        return;
    set_env(anahtar, deger);
}

// Bu boot gercekten bir "isletim sistemi acma denemesi" mi?
// DEGILSE sayilmamali -- yoksa sayac bootloop olmadan da doluyor.
// Onceki surumdeki iki gercek hata:
//   - kapaliyken sarja takmak (POWEROFF_CHARGING) boot denemesi sayiliyordu
//   - recovery'ye girmek boot denemesi sayiliyordu
// Ikisinde de cihaz kullanicinin kontrolunde, bootloop yok.
static int os_boot_denemesi(void) {
    switch (get_bootmode()) {
    case BOOTMODE_NORMAL:
    case BOOTMODE_ALARM:
        return 1;
    default:
        return 0;
    }
}

static void auto_fastboot_check(void) {
    // Kullanici zaten fastboot/recovery/sarj ekranindaysa cihaz kontrol
    // altindadir: sayaci sifirla ve cik.
    if (!os_boot_denemesi()) {
        printf("[autofb] boot modu %d -- deneme sayilmadi, sayac sifir\n",
               get_bootmode());
        set_env_degistiyse(KAERU_ENV_BOOTCOUNT, "0");
        return;
    }

    char *val = get_env(KAERU_ENV_BOOTCOUNT);
    int count = 0;

    if (val) {
        for (const char *p = val; *p >= '0' && *p <= '9'; p++)
            count = count * 10 + (*p - '0');
    }
    count++;

    printf("[autofb] boot denemesi %d / %d\n", count, BOOTLOOP_THRESHOLD);

    if (count >= BOOTLOOP_THRESHOLD) {
        printf("[autofb] esik asildi -> fastboot\n");
        video_printf(" => auto-fastboot (bootloop)\n");
        set_env_degistiyse(KAERU_ENV_BOOTCOUNT, "0");

        // OC otomatik geri alma.
        // Bootloop'un en olasi sebebi overclock ise, kullaniciyi fastboot'ta
        // "simdi ne yapacagim" durumunda birakmak yerine OC'yi burada
        // kapatiyoruz: sonraki reboot stok hizlarda ve temiz acilir.
        // Bilerek AYRI bir sayac kullanmiyoruz -- bootcount basarili
        // acilista sifirlanmadigi icin kendi sayacimiz normal reboot'lari
        // ariza sanip OC'yi durup dururken kapatirdi (yanlis pozitif).
        // Esik zaten sadece gercek ardisik basarisiz boot'larda doluyor.
        if (oc_enabled()) {
            set_env(KAERU_ENV_OC, "0");
            printf("[oc] bootloop -> overclock OTOMATIK KAPATILDI\n");
            video_printf(" => overclock disabled (bootloop)\n");
        }

        set_bootmode(BOOTMODE_FASTBOOT);
        return;
    }

    char buf[12];
    char tmp[12];
    int n = count, i = 0;
    if (n == 0) tmp[i++] = '0';
    while (n > 0 && i < 11) { tmp[i++] = (char)('0' + (n % 10)); n /= 10; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
    set_env_degistiyse(KAERU_ENV_BOOTCOUNT, buf);
}

//
// ---- CP15 durumu ve gecici 1 MB section eslemesi ------------------------
//
// LK'nin MMU eslemesi DRAM log bolgelerini kapsamiyor: 0x4D010000 (ramoops)
// okunmaya calisildiginda LK duser. Ilk deneme L1 sayfa tablosuna dogrudan
// kisa-tanimlayici yazmakti; ISE YARAMADI. Sebebi olculmeden bilinemez:
// MMU kapali olabilir (o zaman TTBR0 anlamsizdir ve yazdigimiz yer rastgele
// bellegi bozar) ya da LPAE/uzun tanimlayici kullaniliyor olabilir (o zaman
// kisa format yanlistir). Bu yuzden once OLCUYORUZ: `oem cpuinfo`.
//
// Esleme artik yalnizca "MMU acik VE LPAE degil" oldugunda deneniyor.
//

static uint32_t cp15_sctlr(void) {
    uint32_t v; __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(v)); return v;
}
static uint32_t cp15_ttbcr(void) {
    uint32_t v; __asm__ volatile("mrc p15, 0, %0, c2, c0, 2" : "=r"(v)); return v;
}
static uint32_t cp15_ttbr0(void) {
    uint32_t v; __asm__ volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(v)); return v;
}
static uint32_t cp15_ttbr1(void) {
    uint32_t v; __asm__ volatile("mrc p15, 0, %0, c2, c0, 1" : "=r"(v)); return v;
}
static uint32_t cp15_mair0(void) {
    uint32_t v; __asm__ volatile("mrc p15, 0, %0, c10, c2, 0" : "=r"(v)); return v;
}
static uint32_t cp15_mair1(void) {
    uint32_t v; __asm__ volatile("mrc p15, 0, %0, c10, c2, 1" : "=r"(v)); return v;
}
static uint32_t cp15_dacr(void) {
    uint32_t v; __asm__ volatile("mrc p15, 0, %0, c3, c0, 0" : "=r"(v)); return v;
}

#define MMU_ON()   ((cp15_sctlr() & 1u) != 0u)
#define MMU_LPAE() ((cp15_ttbcr() & (1u << 31)) != 0u)

// LPAE 4 KB sayfa eslemesi.
//
// Olculdu (oem cpuinfo): TTBCR bit31=1 -> LPAE, T0SZ=0.
//   L1[1]     = 0x4c609003  (tablo)
//   L2[0x268] = 0x4c61f003  (yine TABLO -> L3, yani 2 MB blok DEGIL)
// Hedef 2 MB, 4 KB'lik sayfalarla eslenmis; bu yuzden 0x4D0F0000 okunuyordu
// ama 0x4D010000 okunmuyordu -- o sayfa esli degil. Cozum: L3 girisini
// gecici olarak yazmak.
//
// MAIR0=0xeeaa4400 -> Attr1 = 0x44 = Normal, Inner/Outer Non-cacheable.
// Kernel'in DRAM'e biraktigi veriyi onbellekten degil bellekten okumak
// istedigimiz icin AttrIndx=1 tam istedigimiz sey.
//
// Sayfa tanimlayicisi: [47:12] adres | AF | SH=0 | AP=00 (ayricalikli RW)
//                      | AttrIndx=1 | 0b11 (sayfa),  ust kelimede XN.
#define L3_DESC_LO(pa)  (((pa) & 0xFFFFF000u) | (1u << 10) | (1u << 2) | 3u)
#define L3_DESC_HI      (1u << 22)   /* XN (bit 54) */

// Hedef VA'nin L3 girisinin adresini dondurur; esleme uygun degilse NULL.
static volatile uint64_t *mmu_l3_slot(uint32_t va) {
    volatile uint64_t *l1, *l2, *l3;
    uint64_t d;

    if (!MMU_ON() || !MMU_LPAE())
        return 0;

    l1 = (volatile uint64_t *)(cp15_ttbr0() & 0xFFFFFFE0u);
    d  = l1[(va >> 30) & 3u];
    if ((d & 3u) != 3u)                       /* tablo degilse yuruyemeyiz */
        return 0;

    l2 = (volatile uint64_t *)(uint32_t)(d & 0xFFFFF000u);
    d  = l2[(va >> 21) & 0x1FFu];
    if ((d & 3u) != 3u)                       /* 2 MB blok ya da gecersiz */
        return 0;

    l3 = (volatile uint64_t *)(uint32_t)(d & 0xFFFFF000u);
    return &l3[(va >> 12) & 0x1FFu];
}

static void mmu_sync(volatile uint64_t *slot) {
    arch_clean_invalidate_cache_range((uintptr_t)slot, 8);
    uint32_t zero = 0;
    __asm__ volatile(
        "dsb\n\t"
        "mcr p15, 0, %0, c8, c7, 0\n\t"   /* TLBIALL */
        "dsb\n\t"
        "isb\n\t"
        :: "r"(zero) : "memory");
}

// Sayfayi 1:1 esler; basarili ise 1 doner ve eski tanimlayici *old_out'a yazilir.
static int mmu_map_page(uint32_t va, uint64_t *old_out) {
    volatile uint64_t *slot = mmu_l3_slot(va);

    if (!slot)
        return 0;
    *old_out = *slot;
    *slot = (uint64_t)L3_DESC_LO(va) | ((uint64_t)L3_DESC_HI << 32);
    mmu_sync(slot);
    return 1;
}

static void mmu_restore_page(uint32_t va, uint64_t old) {
    volatile uint64_t *slot = mmu_l3_slot(va);

    if (!slot)
        return;
    *slot = old;
    mmu_sync(slot);
}

// ---- kucuk hex yazici (fastboot_info string ister) ----------------------
static void hexline(const char *label, uint32_t v) {
    static char b[48];
    const char *h = "0123456789abcdef";
    int i = 0, k;

    while (*label && i < 24)
        b[i++] = *label++;
    b[i++] = '='; b[i++] = '0'; b[i++] = 'x';
    for (k = 28; k >= 0; k -= 4)
        b[i++] = h[(v >> k) & 0xF];
    b[i] = '\0';
    fastboot_info(b);
}

static void hexline64(const char *label, uint64_t v) {
    hexline(label, (uint32_t)(v >> 32));
    hexline(label, (uint32_t)v);
}

// fastboot oem cpuinfo -- SALT OKUNUR, hicbir sey degistirmez
static void cmd_cpuinfo(const char *arg, void *data, unsigned sz) {
    uint32_t sctlr = cp15_sctlr(), ttbcr = cp15_ttbcr();

    hexline("SCTLR", sctlr);
    hexline("TTBCR", ttbcr);
    hexline("TTBR0", cp15_ttbr0());
    hexline("TTBR1", cp15_ttbr1());
    hexline("DACR",  cp15_dacr());
    fastboot_info((sctlr & 1u) ? "MMU=ON" : "MMU=OFF");
    fastboot_info((ttbcr & (1u << 31)) ? "LPAE=YES" : "LPAE=NO");

    hexline("MAIR0", cp15_mair0());
    hexline("MAIR1", cp15_mair1());

    if ((sctlr & 1u) && (ttbcr & (1u << 31))) {
        // LPAE: T0SZ=0 -> L1'de 1 GB'lik 4 giris, L2'de 2 MB'lik bloklar.
        // Hedef 0x4D000000: L1 idx = VA>>30, L2 idx = (VA>>21)&0x1FF.
        uint64_t *l1 = (uint64_t *)(cp15_ttbr0() & 0xFFFFFFE0u);
        uint64_t d1 = l1[1];                       /* VA 0x40000000-0x7FFFFFFF */

        hexline64("L1[1]", d1);
        if ((d1 & 3u) == 3u) {                     /* tablo tanimlayicisi */
            uint64_t *l2 = (uint64_t *)(uint32_t)(d1 & 0xFFFFF000u);
            hexline64("L2[0x268]", l2[0x68]);      /* 0x4D000000 */
            hexline64("L2[0x260]", l2[0x60]);      /* 0x4C000000 (LK'nin kendisi) */
        }
    }
    fastboot_okay("");
}
// ---- log bolgesi okuyucular (fastboot komutlari) -------------------------
//
// Boot etmeyen bir kernel'in logunu yakalamanin en iyi ani, cihazin
// auto-fastboot ile durdugu andir: DRAM'deki ramoops/mboot_params tamponlari
// o noktada hala el degmemistir. Kurtarma icin fastboot'tan gecen 72 MB
// onlari eziyor -- uc testte de kernel logunu bu yuzden kaybettik.
//
// Hepsi SALT OKUNUR ve yalnizca fastboot komut isleyicisinde calisir; boot
// yoluna tek satir eklemez. (Cihazi tuglalayan onceki deneme board_late_init
// icinde, yani boot'un ortasinda is yapiyordu.)
//
#define RAMOOPS_BASE    0x4D010000u   // ramoops.mem_address (cmdline'dan)
#define RAMOOPS_DEFLEN  0x00040000u   // ramoops.console_size
#define MBOOT_DRAM      0x4D0F0000u   // mboot_params (AEE RAM console) DRAM
#define MBOOT_DRAM_SZ   0x00010000u
#define SRAM_LOG_BASE   0x0010E000u   // log_store + RR basligi (DBGC)
#define SRAM_LOG_SIZE   0x00002000u

#define DUMP_LINE   56
#define DUMP_CHUNK  2048              // esleme yalnizca bu kopya suresince acik
#define DUMP_MAXLEN 0x00010000u

static uint8_t dump_buf[DUMP_CHUNK];

// Beyaz liste. DRAM'i genis biraktik; guvenli (TEE/EMI-MPU korumali) bolgeler
// MMU'dan bagimsiz olarak zaten donanimda reddedilir ve LK duser -- cihaz
// kendiliginden resetlenip acilir, tugla olusmaz.
static int mem_allowed(uint32_t addr, uint32_t len) {
    uint32_t end = addr + len;

    if (len == 0 || len > DUMP_MAXLEN)
        return 0;
    if (end < addr)                                  // tasma
        return 0;
    if (addr >= 0x00100000u && end <= 0x00120000u)   // SRAM
        return 1;
    if (addr >= 0x40000000u && end <= 0xC0000000u)   // DRAM
        return 1;
    return 0;
}

static void emit_lines(const uint8_t *p, uint32_t n) {
    static char line[DUMP_LINE + 1];
    static int col;
    uint32_t i;

    for (i = 0; i < n; i++) {
        unsigned char c = p[i];

        if (c == '\n' || col == DUMP_LINE) {
            line[col] = '\0';
            if (col)
                fastboot_info(line);
            col = 0;
            if (c == '\n')
                continue;
        }
        line[col++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
    }
}

static void emit_flush(void) {
    // emit_lines'in kalan yarim satirini bosalt
    fastboot_info("");
}

static void dump_region(uint32_t addr, uint32_t len) {
    uint32_t done = 0;

    while (done < len) {
        uint32_t a    = addr + done;
        uint32_t page = a & 0xFFFFF000u;
        uint32_t n    = len - done;
        uint32_t to_page_end = (page + 0x1000u) - a;
        uint64_t old = 0;
        uint32_t i;
        int mapped;

        if (n > DUMP_CHUNK)    n = DUMP_CHUNK;
        if (n > to_page_end)   n = to_page_end;   /* sayfa sinirini asma */

        // Esleme YALNIZCA kopyalama suresince acik: fastboot_info() cagrisi
        // sirasinda acik kalsa ve LK'nin USB tamponlari o sayfada olsa bozardik.
        mapped = mmu_map_page(page, &old);
        for (i = 0; i < n; i++)
            dump_buf[i] = *(const volatile unsigned char *)(a + i);
        if (mapped)
            mmu_restore_page(page, old);

        emit_lines(dump_buf, n);
        done += n;
    }
    emit_flush();
}

static uint32_t parse_hex(const char **s) {
    const char *p = *s;
    uint32_t v = 0;

    while (*p == ' ')
        p++;
    for (;;) {
        char c = *p;
        int d;

        if (c >= '0' && c <= '9')      d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = (v << 4) | (uint32_t)d;
        p++;
    }
    *s = p;
    return v;
}

// fastboot oem ramoops [uzunluk_hex]
static void cmd_dump_ramoops(const char *arg, void *data, unsigned sz) {
    const char *p = arg ? arg : "";
    uint32_t len = parse_hex(&p);

    if (len == 0 || len > DUMP_MAXLEN)
        len = RAMOOPS_DEFLEN > DUMP_MAXLEN ? DUMP_MAXLEN : RAMOOPS_DEFLEN;
    fastboot_info("[selene] ramoops @0x4D010000");
    dump_region(RAMOOPS_BASE, len);
    fastboot_okay("");
}

// fastboot oem rrlog  -- mboot_params (AEE RAM console)
static void cmd_dump_rrlog(const char *arg, void *data, unsigned sz) {
    fastboot_info("[selene] mboot_params @0x4D0F0000");
    dump_region(MBOOT_DRAM, MBOOT_DRAM_SZ);
    fastboot_okay("");
}

// fastboot oem sram   -- log_store + RR basligi (DBGC)
static void cmd_dump_sram(const char *arg, void *data, unsigned sz) {
    fastboot_info("[selene] sram @0x0010E000");
    dump_region(SRAM_LOG_BASE, SRAM_LOG_SIZE);
    fastboot_okay("");
}


// fastboot oem lsm-version
static void cmd_lsm_version(const char *arg, void *data, unsigned sz) {
    fastboot_info(LSM_NAME " v" LSM_VERSION " (selene LK eklentisi)");
    fastboot_info("  auto-fastboot: 5 basarisiz boot -> fastboot");
    fastboot_info("  log: oem ramoops / rrlog / sram / rdmem / cpuinfo");
    fastboot_info("  taban: kaeru AGPL-3.0 (KAERU Labs, vrdons fork)");
    fastboot_okay("");
}

// fastboot oem rdmem <adres_hex> <uzunluk_hex>
static void cmd_rdmem(const char *arg, void *data, unsigned sz) {
    const char *p = arg ? arg : "";
    uint32_t addr = parse_hex(&p);
    uint32_t len  = parse_hex(&p);

    if (!mem_allowed(addr, len)) {
        fastboot_fail("adres/uzunluk izinli bolgede degil");
        return;
    }
    dump_region(addr, len);
    fastboot_okay("");
}

//
// ---- CPU OVERCLOCK (yalnizca LK) --------------------------------------
//
// Bayrak LK env'inde tutulur; Android'den (root dahil) degistirilemez.
// Acikken her boot'ta kernel cmdline'ina " selene.oc=1" eklenir. Kernel
// bunu gorunce MediaTek'in KENDI "PRO" OPP tablosuna gecer:
//     kucuk kume (A55)  1800 -> 2000 MHz
//     buyuk kume (A75)  2000 -> 2202 MHz
// Voltajlar uydurma DEGIL, ayni SoC icin ureticinin dogruladigi ciftler.
//
static bool oc_enabled(void) {
    char *v = get_env(KAERU_ENV_OC);
    return (v && v[0] == '1');
}

// ---------------------------------------------------------------------------
// Ekranda kirmizi metin.
//
// Adresler lk_payload.bin uzerinden tersine muhendislikle bulundu:
//   video_set_fg_color(r,g,b) @ 0x4C43AD6C   video_set_bg_color @ 0x4C43AE78
// Ikisi de "%s,(r-%u,g-%u,b-%u)" formatiyla kendini logluyor -> imza kesin.
//
// Fonksiyon once bir ekran-bilgi yapisini dereference ediyor
// (ldr lr,[DISP_INFO]; ldr sl,[lr,#0x28]). Ekran init edilmeden cagrilirsa
// data abort olur; bu yuzden NULL kontrolu ZORUNLU.
//
// Renk 3 global'de tutuluyor: fg, bg ve turetilmis fg^bg. Sadece fg'yi geri
// yazmak XOR'u bayat birakip metin cizimini bozar -> ikisi birlikte saklanir.
// Boylece "varsayilan herhalde beyazdir" tahminine hic gerek kalmiyor.
#define SELENE_DISP_INFO   0x4C574D90u
#define SELENE_FG_COLOR    0x4C574D70u
#define SELENE_FG_XOR_BG   0x4C574D94u

#define OC_MMIO32(a)       (*(volatile unsigned int *)(unsigned long)(a))

static unsigned int oc_fg_kayit;
static unsigned int oc_xor_kayit;
static int oc_renk_kayitli;

static int oc_renk_var(void) {
#ifdef CONFIG_VIDEO_COLOR_SUPPORT
    return OC_MMIO32(SELENE_DISP_INFO) != 0u;
#else
    return 0;
#endif
}

static void oc_kirmizi(void) {
#ifdef CONFIG_VIDEO_COLOR_SUPPORT
    if (!oc_renk_var())
        return;
    if (!oc_renk_kayitli) {
        oc_fg_kayit   = OC_MMIO32(SELENE_FG_COLOR);
        oc_xor_kayit  = OC_MMIO32(SELENE_FG_XOR_BG);
        oc_renk_kayitli = 1;
    }
    video_set_fg_color(255, 0, 0);   // saf kirmizi
#endif
}

static void oc_renk_geri(void) {
#ifdef CONFIG_VIDEO_COLOR_SUPPORT
    if (!oc_renk_kayitli)
        return;
    OC_MMIO32(SELENE_FG_COLOR)  = oc_fg_kayit;
    OC_MMIO32(SELENE_FG_XOR_BG) = oc_xor_kayit;
    oc_renk_kayitli = 0;
#endif
}

// Tamami BUYUK HARF olan satirlar saf kirmizi basilir.
// Not: video_printf'e her zaman dizge SABITI verilir -- KAERU sarmalayicisi
// varargs'i r1-r3'te tasimaya guveniyor, format argumani kullanmamak en guvenlisi.
#define OC_CAPS(lit) do { oc_kirmizi(); video_printf(lit); oc_renk_geri(); } while (0)

static void oc_warning_screen(void) {
    video_printf("\n");
    video_printf("=====================================================\n");
    OC_CAPS("   !!!  CPU OVERCLOCK  --  READ THIS CAREFULLY  !!!   \n");
    video_printf("=====================================================\n");
    video_printf("\n");
    video_printf("You are about to RAISE the CPU clock ABOVE the limits\n");
    video_printf("your device was sold with:\n");
    video_printf("\n");
    video_printf("    little cores (A55)   1800 MHz  ->  2000 MHz\n");
    video_printf("    big cores    (A75)   2000 MHz  ->  2202 MHz\n");
    video_printf("\n");
    video_printf("While enabled, the CPU is held at maximum frequency,\n");
    video_printf("thermal throttling and power limits are DISABLED.\n");
    video_printf("\n");
    OC_CAPS("THIS IS INTENDED FOR GAMING AND HEAVY WORKLOADS ONLY.\n");
    OC_CAPS("USING AN EXTERNAL COOLER IS STRONGLY RECOMMENDED.\n");
    video_printf("\n");
    video_printf("The phone WILL run hot. A hard emergency shutdown at\n");
    video_printf("about 110 C is the only protection left in place -- it\n");
    video_printf("prevents fire and permanent thermal death, nothing more.\n");
    video_printf("\n");
    OC_CAPS("ONLY ENABLE THIS IF YOU KNOW WHAT YOU ARE DOING.\n");
    OC_CAPS("ANY DAMAGE, INSTABILITY, DATA LOSS OR REDUCED LIFESPAN\n");
    OC_CAPS("OF THE DEVICE IS *** YOUR OWN RESPONSIBILITY ***.\n");
    video_printf("\n");
    video_printf("-----------------------------------------------------\n");
    OC_CAPS("   VOLUME UP    =  I ACCEPT, ENABLE OVERCLOCK\n");
    OC_CAPS("   VOLUME DOWN  =  CANCEL, KEEP STOCK CLOCKS\n");
    video_printf("-----------------------------------------------------\n");
    video_printf("\n");
}

// Tus bekleme sinirli: kullanici hicbir seye basmazsa komut asili kalmasin,
// zaman asiminda GUVENLI tarafa (iptal) duselim.
#define OC_KEY_WAIT_SPINS  40000000u

static void cmd_oc_on(const char *arg, void *data, unsigned sz) {
    if (oc_enabled()) {
        fastboot_info("overclock is already ENABLED");
        fastboot_info("use 'fastboot oem cpuoverclock off' to disable");
        fastboot_okay("");
        return;
    }

    oc_warning_screen();
    fastboot_info("WARNING shown on device screen -- read it");
    fastboot_info("VOLUME UP = accept, VOLUME DOWN = cancel");

    for (unsigned spins = 0; spins < OC_KEY_WAIT_SPINS; spins++) {
        if (mtk_detect_key(VOLUME_UP)) {
            set_env(KAERU_ENV_OC, "1");
            OC_CAPS("\n  >>> OVERCLOCK ENABLED <<<\n");
            printf("[oc] kullanici onayladi, kaeru.oc=1\n");
            fastboot_info("overclock ENABLED -- reboot to apply");
            fastboot_okay("");
            return;
        }
        if (mtk_detect_key(VOLUME_DOWN)) {
            video_printf("\n  cancelled, stock clocks kept\n");
            printf("[oc] kullanici iptal etti\n");
            fastboot_info("cancelled -- stock clocks kept");
            fastboot_okay("");
            return;
        }
    }

    video_printf("\n  timed out, stock clocks kept\n");
    printf("[oc] tus bekleme zaman asimi, iptal\n");
    fastboot_fail("timed out waiting for confirmation");
}

static void cmd_oc_off(const char *arg, void *data, unsigned sz) {
    set_env(KAERU_ENV_OC, "0");
    printf("[oc] kapatildi\n");
    fastboot_info("overclock DISABLED -- reboot to apply");
    fastboot_okay("");
}

// Bayrak acikken cmdline'a token ekle. cmdline_replace yalnizca var olan bir
// parametreyi degistiriyor, o yuzden tampona dogrudan ekliyoruz. Tasma
// kontrolu sart: LK tamponu CMDLINE_LEN (2048) bayt.
// ---------------------------------------------------------------------------
// LK -> kernel kanali: /chosen/bootargs
//
// Adresler lk_payload.bin uzerinden cikarildi (2026-09-02):
//   0x4C429D88  bootargs kurucu (app/mt_boot/bootargs.c). Tek argumanli,
//               donus degeri cagri yerinde KULLANILMIYOR -> sarmalanabilir.
//   0x4C4258A8  onu cagiran TEK yer (bl). Kanca noktamiz.
//   0x4C517688  cmdline tamponunun isaretcisini tutan global.
//   0x4C566524  tamponun kendisi, 0x800 (2048) bayt.
//
// NEDEN GEREKLI: onceki surum cmdline'i board_late_init icinde yaziyordu,
// ama tampon o anda HENUZ BOS -- olculdu, log "uzunluk=0" diyordu. Ustelik
// KAERU'nun RECOVERY_CMDLINE2 adresi (0x4C565F1E) tamponun 1542 bayt
// ONCESINE dusuyor; oraya yazmak LK bellegini bozup KERNEL PANIGI ve
// bootloop uretiyordu. Bayrak kernel'e HIC ulasmiyordu, yani dort tur boyunca
// "cip PRO tablosunu kaldiramiyor" diye tartistigimiz sey hic denenmemisti.
#define LK_BOOTARGS_BUILD  0x4C429D88u
#define LK_BOOTARGS_CALL   0x4C4258A8u
#define LK_CMDLINE_PTR     0x4C517688u

#define OC_CMDLINE_MAX 2048u

static void oc_append_cmdline_at(uint32_t addr) {
    if (!addr)
        return;

    char *cl = (char *)addr;
    unsigned len = 0;

    while (len < OC_CMDLINE_MAX && cl[len] != '\0')
        len++;
    if (len >= OC_CMDLINE_MAX)
        return;                       // sonlandirilmamis -> dokunma

    // zaten eklenmisse tekrar ekleme
    for (unsigned i = 0; i + 11 < len; i++) {
        if (cl[i] == 's' && cl[i+1] == 'e' && cl[i+2] == 'l' &&
            cl[i+3] == 'e' && cl[i+4] == 'n' && cl[i+5] == 'e' &&
            cl[i+6] == '.' && cl[i+7] == 'o' && cl[i+8] == 'c')
            return;
    }

    printf("[oc] cmdline @0x%08X uzunluk=%u\n", addr, len);

    const char *tok = OC_CMDLINE_TOKEN;
    unsigned tl = 0;
    while (tok[tl])
        tl++;
    if (len + tl + 1 >= OC_CMDLINE_MAX)
        return;                       // sigmiyor -> vazgec

    for (unsigned i = 0; i <= tl; i++)
        cl[len + i] = tok[i];

    printf("[oc] cmdline'a eklendi @0x%08X\n", addr);
}

//
// LK'nin cmdline kurucusunu sarmalar. board_early_init'te PATCH_CALL ile
// 0x4C4258A8'deki bl buraya yonlendirilir.
//
// oc_enabled() kontrolu KURULUMDA degil BURADA yapilir: board_early_init
// calisirken env alani henuz hazir degil (ayni tuzak getvar cpuclock'u da
// bozmustu), ama bu kanca boot imaji islenirken calisiyor ve env okunabiliyor.
//
static void __attribute__((unused)) oc_bootargs_hook(void *arg) {
    // 1) HER ZAMAN once orijinali cagir -- cmdline normal sekilde kurulsun.
    ((void (*)(void *))(LK_BOOTARGS_BUILD | 1))(arg);

    // 2) OC kapaliysa hicbir seye dokunma.
    if (!oc_enabled())
        return;

    // 3) Tampon artik dolu; token'i ekle. Isaretciyi sabit yazmak yerine
    //    global'den okuyoruz, boylece LK yerlesimi degisirse kendini uyarlar.
    uint32_t buf = OC_MMIO32(LK_CMDLINE_PTR);

    if (!buf) {
        printf("[oc] cmdline isaretcisi NULL -- eklenmedi\n");
        return;
    }
    oc_append_cmdline_at(buf);
}

//
// LK -> kernel kanali: AYRILMIS DRAM KELIMESI
//
// Cmdline yolu bu cihazda kullanilamadi: cmdline tamponuna ekleme yapmak --
// kernel'in tamamen yok saydigi bir token bile olsa -- kernel panigi
// uretiyordu (olculdu, exp_type 0x2). Bkz. board-selene.c gecmisi.
//
// Bunun yerine tek bir 64-bit sihirli deger yaziyoruz. Secilen adres
// ramoops oyugunun son 64 KB'i: kernel'e "ayrilmis" diye bildirilen bolge
// (ramoops.mem_address=0x4d010000 mem_size=0xe0000 -> 0x4D010000..0x4D0F0000)
// ve bu cihazda pstore hic kurulmadigi icin oraya kimse dokunmuyor.
//
// Hata modu IYI HUYLU: kernel cop okursa sihirli deger tutmaz ve OC KAPALI
// sayilir -- yani yanlis giderse cihaz stok hizlarda acilir, panik olmaz.
//
// Bayrak her boot'ta YENIDEN yazilir; OC kapaliyken acikca SIFIRLANIR ki
// onceki boot'tan kalan deger yanlislikla OC'yi acmasin.
//
#define OC_SIGNAL_ADDR  0x4D0E0000u
#define OC_SIGNAL_HI    0x53454C45u   /* "SELE" */
#define OC_SIGNAL_LO    0x4F433031u   /* "OC01" */

static void oc_signal_write(void) {
    volatile uint32_t *p = (volatile uint32_t *)(unsigned long)OC_SIGNAL_ADDR;

    /*
     * SADECE gercek OS boot'unda yaz.
     *
     * Neden: bu sinyalin tek okuyucusu kernel. Fastboot/recovery/sarj
     * modunda kernel hic acilmiyor, dolayisiyla yazmanin faydasi YOK --
     * ama zarari olabilir: o modlarda LK bellekte KALIYOR ve DRAM'i farkli
     * kullaniyor (indirme tamponu vb.). 0x4D0E0000'in bos oldugunu yalnizca
     * NORMAL boot icin olctuk; fastboot modu icin olcmedik.
     *
     * Belirti (2026-09-03): "adb reboot bootloader" sonrasi LOGO HIC
     * CIKMADAN siyah ekran + bootloop. board_late_init logodan ONCE
     * calisiyor, yani buradaki bir bozulma tam bu belirtiyi verir.
     * Kesin sebep kanitlanmadi, ama bu kontrol her durumda DOGRU: yazmanin
     * anlamli oldugu tek mod normal boot.
     */
    if (!os_boot_denemesi()) {
        printf("[oc] boot modu %d -- DRAM sinyaline DOKUNMA\n", get_bootmode());
        return;
    }

    if (oc_enabled()) {
        p[0] = OC_SIGNAL_HI;
        p[1] = OC_SIGNAL_LO;
        printf("[oc] DRAM sinyali YAZILDI @0x%08X\n", OC_SIGNAL_ADDR);
    } else {
        p[0] = 0;
        p[1] = 0;
        printf("[oc] DRAM sinyali temizlendi @0x%08X\n", OC_SIGNAL_ADDR);
    }
}

static void oc_apply(void) {
    if (!oc_enabled())
        return;

    printf("[oc] AKTIF -- PRO OPP tablosu istenecek\n");
    video_printf(" [oc] overclock active\n");

    // Cmdline'a BURADA yazilmaz -- tampon bu asamada bos. Yazma isi
    // oc_bootargs_hook icinde, cmdline kurulduktan SONRA yapiliyor.
}

// Bootcount yardimcilari: reset-bootcount.sh misc'e yaziyordu ve YANLISTI
// (LK sayaci env'de tutuyor). Dogrusu fastboot'tan bu komutlar.
static void cmd_bootcount_reset(const char *arg, void *data, unsigned sz) {
    set_env(KAERU_ENV_BOOTCOUNT, "0");
    printf("[autofb] sayac sifirlandi\n");
    fastboot_info("bootcount reset to 0");
    fastboot_okay("");
}

static void cmd_bootcount_get(const char *arg, void *data, unsigned sz) {
    char *v = get_env(KAERU_ENV_BOOTCOUNT);
    fastboot_info(v ? v : "0");
    fastboot_okay("");
}

static void spoof_lock_state(void) {
    uint32_t addr = 0;

    // With the device reporting as locked, the fastboot dispatcher
    // gates every command on lock/secure state, so stock commands
    // (flash, erase, ...) get rejected too. Its pre-handler gate has
    // several skip branches; replace the first one with a direct jump
    // to the handler dispatch so every command runs regardless of lock
    // state.
    //
    // The dispatch reads only the command struct and stack, so it's
    // safe to skip the gate.
    addr = SEARCH_PATTERN(LK_START, LK_END, 0x4C8C, 0xE92D, 0x4880, 0xB08D);
    if (addr) {
        printf("Found fastboot command processor at 0x%08X\n", addr);

        // cbz r0, <alt gate> -> b <handler dispatch> (0xE00F)
        PATCH_MEM(addr + 0x192, 0xE00F);
    }

    int spoofing = is_spoofing_enabled();
    fastboot_publish("is-spoofing", spoofing ? "1" : "0");

    if (!spoofing) {
        printf("Bootloader lock status spoofing disabled.\n");
        return;
    }

    printf("Bootloader lock status spoofing enabled, applying patches.\n");

    // On most MediaTek devices, lock state is fetched by calling
    // seccfg_get_lock_state() directly. Some vendors (e.g. Xiaomi)
    // add a wrapper that also checks a custom lock mechanism, but
    // this device does not have one.
    //
    // Unlike other LK images that route all callers through a b.w
    // thunk (which can be redirected with a single patch), this LK
    // calls seccfg_get_lock_state() directly, so we patch the
    // function body itself. The patch forces it to store 4 into the
    // output parameter and return 0, which the callers interpret as
    // "success, device locked" (only state 3 means unlocked here;
    // state 1/2 are special "default" states that trigger first-boot
    // transition paths, so we report the explicit locked state 4).
    addr = SEARCH_PATTERN(LK_START, LK_END, 0xB1D0, 0xB510, 0x4604, 0xF7FF, 0xFFDD);
    if (addr) {
        printf("Found seccfg_get_lock_state at 0x%08X\n", addr);
        PATCH_MEM(addr + 6,
            0x2300,  // movs r3, #4  (LKS_LOCK)
            0x6023,  // str r3, [r4, #0]
            0x2000,  // movs r0, #0
            0xBD10   // pop {r4, pc}
        );
    }

    // AVB adds device state info to the kernel cmdline, but it
    // keeps showing "unlocked" even when we want it to say "locked".
    // This patch forces the cmdline to always use the "locked"
    // string instead of checking the actual device state.
    addr = SEARCH_PATTERN(LK_START, LK_END, 0xE92D, 0x4FF0, 0x4691, 0xF102);
    if (addr) {
        printf("Found AVB cmdline function at 0x%08X\n", addr);

        // NOP out the code that checks the actual device state,
        // forcing libavb to always use the "locked" string.
        NOP(addr + 0x9C, 4);
    }

    // Hook cmdline_pre_process so handle_recovery_boot() can flip
    // verifiedbootstate before LK hands the cmdline to the kernel.
    addr = SEARCH_PATTERN(LK_START, LK_END, 0xF001, 0xF83D, 0x3508, 0xF8DF);
    if (addr) {
        printf("Found cmdline_pre_process at 0x%08X\n", addr);
        PATCH_CALL(addr, (void *)handle_recovery_boot, TARGET_THUMB);
    }
}

void board_early_init(void) {
    printf("Entering early init for Redmi 10 2022 (selene)\n");

    uint32_t addr = 0;

    // Overclock bayragini kernel'e ulastiran kanca. Burada kuruluyor cunku
    // LK metninin yazilabilir oldugu kanitli asama burasi (asagidaki diger
    // yamalar da burada). Kancanin kendisi OC kapaliyken orijinali cagirip
    // donuyor, yani stok davranis degismiyor.
    // NOT: bootargs kancasi GERI ALINDI (2026-09-02).
    // Cmdline tamponuna ekleme yapmak -- ZARARSIZ bir token bile olsa --
    // kernel panigi uretiyordu. Olculdu: kernel tarafindan tamamen yok
    // sayilan " selene.octest=1" ile de bootloop, exp_type 0x2 (kernel
    // panigi, LK cokmesi degil). Yani sorun overclock DEGIL, cmdline
    // tamponuna dokunmanin kendisi. Bayrak baska bir kanalla tasinacak.

    // Regardless of whether spoofing is enabled, we always need to
    // disable image authentication. The user may just be using this
    // custom LK to unlock their device, or they may be spoofing
    // where the locked state would enforce verification.
    //
    // Forcing get_vfy_policy to return 0 skips certificate
    // verification for all partitions and firmware images (boot,
    // recovery, dtbo, SCP, etc.) so the device can boot with
    // modified or unsigned images.
    addr = SEARCH_PATTERN(LK_START, LK_END, 0xB508, 0xF7FF, 0xFF63, 0xF3C0);
    if (addr) {
        printf("Found get_vfy_policy at 0x%08X\n", addr);
        FORCE_RETURN(addr, 0);
    }

    // Since we're spoofing the LKS_STATE as locked, get_dl_policy would normally
    // restrict fastboot downloads/flashing based on security policy. Force it to
    // return 0 to bypass these restrictions and allow unrestricted flashing.
    addr = SEARCH_PATTERN(LK_START, LK_END, 0xB508, 0xF7FF, 0xFF5D, 0xF000);
    if (addr) {
        printf("Found get_dl_policy at 0x%08X\n", addr);
        FORCE_RETURN(addr, 0);
    }

    // Since we report the device as locked, AVB treats a bad signature,
    // hash mismatch, rollback or rejected key as fatal and won't boot
    // modified or resigned images. Force it into "allow verification
    // error" mode, the same path AVB uses when unlocked, so it tolerates
    // any vbmeta and still builds slot_data and the kernel cmdline.
    //
    // We patch avb_slot_verify to force that flag on. This covers the
    // recoverable errors above. Structurally invalid vbmeta is still
    // rejected by AVB, but get_vfy_policy above already ungates boot.
    addr = SEARCH_PATTERN(LK_START, LK_END,
                          0xF005, 0x0301, 0xF083, 0x0A01, 0x930D, 0x9B70);
    if (addr) {
        printf("Found avb_slot_verify allow-error gate at 0x%08X\n", addr);
        // and r3, r5, #1  ->  mov.w r3, #1
        PATCH_MEM(addr, 0xF04F, 0x0301);
    }

    // The environment area isn't initialized yet when board_early_init
    // runs, so any get_env calls would return NULL at this stage. We
    // hook a printf call in platform_init that runs right after env
    // initialization completes, it's a convenient entry point since
    // the call itself is non-essential and we need the env to be ready
    // before applying our lock state patches.
    addr = SEARCH_PATTERN(LK_START, LK_END, 0xF037, 0xFD10, 0x6823, 0x2001);
    if (addr) {
        printf("Found env_init_done at 0x%08X\n", addr);
        PATCH_CALL(addr, (void*)spoof_lock_state, TARGET_THUMB);
    }

    // Register our custom fastboot commands.
    fastboot_register("oem bldr_spoof", cmd_spoof_bootloader_lock, 1);

    // Log okuyucular: cihaz auto-fastboot ile durdugunda DRAM'deki
    // ramoops/mboot_params tamponlari hala el degmemis olur. Salt
    // okunur, boot yolunda calismaz.
    fastboot_register("oem ramoops", cmd_dump_ramoops, 1);
    fastboot_register("oem rrlog", cmd_dump_rrlog, 1);
    fastboot_register("oem sram", cmd_dump_sram, 1);
    fastboot_register("oem rdmem", cmd_rdmem, 1);
    fastboot_register("oem cpuinfo", cmd_cpuinfo, 1);
    fastboot_register("oem lsm-version", cmd_lsm_version, 1);

    // CPU overclock -- yalnizca buradan acilip kapatilir. Bilerek
    // hicbir sysfs/proc anahtari YOK: Android tarafi (root dahil) bu
    // bayragi degistiremez -- LK env'ine yazamaz ve calisirken
    // kernel cmdline'ini degistiremez.
    //
    // !!! FASTBOOT KOMUT ADI SECIMI -- OKUMADAN DEGISTIRME !!!
    //
    // MTK LK'nin fastboot dagiticisi ONEK eslemesi yapar: kayitli bir ismin,
    // gelen komut dizesinin basiyla eslesmesi yeterlidir. Yani KISA olan
    // kazanir ve stok komutlar bizimkilerden once denenir.
    //
    // 2026-09-02'de bu yuzden VERI KAYBI yasandi: komutlar
    // "oem unlock cpuclock" / "oem lock cpuclock" olarak kayitliydi. Stok LK
    // "oem unlock" (0x4C48EF24) ve "oem lock" (0x4C48EF30) dizelerini kayitli
    // tutuyor; bunlar bizimkilerin ONEKI oldugu icin komut bize hic ulasmadi,
    // dogrudan GERCEK bootloader kilitleme yoluna dustu. "oem lock cpuclock"
    // seccfg'yi kilitledi; kilit durumu TEE'ye bagli oldugu icin /data
    // sifreleme anahtari gecersizlesti ve tum kullanici verisi gitti.
    //
    // KURAL: hicbir stok komut, bizim komut ismimizin oneki OLMAMALI.
    // Dogrulama: utils tarafinda stok lk_a-device.bin icindeki "oem ..."
    // dizeleri taranip carpisma kontrol edilir (0 carpisma dogrulandi).
    // Bosluklu alt-komut yerine tireli tek parca isim tercih edilir.
    //
    fastboot_register("oem cpuoverclock on", cmd_oc_on, 1);
    fastboot_register("oem cpuoverclock off", cmd_oc_off, 1);

    // Bootcount: eski reset-bootcount.sh misc'e yaziyordu, YANLISTI.
    // "oem bootcount" , "oem bootcount reset"in onegiydi -- ayni tuzak.
    fastboot_register("oem bootcount-reset", cmd_bootcount_reset, 1);
    fastboot_register("oem bootcount-get", cmd_bootcount_get, 1);
    fastboot_publish("lsm-version", LSM_NAME " v" LSM_VERSION);
    // NOT: "cpuclock" burada YAYINLANMAZ. board_early_init calisirken env
    // alani henuz hazir degil, oc_enabled() daima false doner ve getvar
    // kalici olarak "stock" gosterirdi (komut handler'i dogruyu soylerken
    // getvar yalan soyluyordu -- 2026-09-02'de tam olarak bu yasandi).
    // Yayin board_late_init'e tasindi; orada env okunabiliyor.
    printf("[LSM] " LSM_NAME " v" LSM_VERSION " hazir\n");
}

void board_late_init(void) {
    printf("Entering late init for Redmi 10 2022 (selene)\n");

    // env burada hazir -- cpuclock'u ancak simdi dogru yayinlayabiliriz.
    fastboot_publish("cpuclock", oc_enabled() ? "overclocked" : "stock");

    // Sinyal HER boot'ta yazilir -- OC kapaliyken temizlenmesi sart,
    // yoksa onceki boot'tan kalan deger OC'yi kendiliginden acardi.
    oc_signal_write();

    oc_apply();
    auto_fastboot_check();
}
