#!/bin/bash
# Auto-fastboot sayacini fastboot'tan sifirla.
#
# ONEMLI: sayac misc'te DEGIL, LK'nin get_env/set_env mekanizmasinda
# (kaeru.bootcount). Bu yuzden sadece FASTBOOT'tan erisilebilir, adb'den
# calisan bir Android script'i ile degil. Normal kullanimda gerek yok --
# esik 5 oldugu icin tek reboot'lar sorun cikarmiyor.
#
# Kullanim: cihaz fastboot'tayken  bash reset-bootcount.sh
set -e
fastboot oem env set kaeru.bootcount 0
fastboot oem env get kaeru.bootcount
