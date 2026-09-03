# patches/

## KernelSU-Next-selene-4.19.patch

KernelSU-Next submodule'u upstream commit'inde (v3.1.0-legacy-susfs) duruyor.
Bu agacta derlemek icin uzerine bu yama uygulanmali: 4.19'da otomatik hook
altyapisi yok, manuel hook'lar kullaniliyor ve Kbuild buna gore sadelestirildi.

Uygulama:

    git submodule update --init
    cd KernelSU-Next && git apply ../patches/KernelSU-Next-selene-4.19.patch

Yama uygulanmadan derlerseniz Kbuild "No hooks were defined" ile durur.
