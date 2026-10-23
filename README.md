xsystem4
========

> **最新狀態（2026-10-23）：v14 滾輪程式 `f693086` 已推送，三方雜湊一致，parent `d9d13e8`，libsys4 `247f544`。提交前／後預設與 GBK 各 115 模式 PASS、sanitizer 0；最終 parent before-check 為 14/15 案語義失敗、舊版防守案通過。提交前／後九路線日誌與操作門檻通過；提交前片頭 50/50，提交後首輪嚴格 48/50 失敗保留，同 binary 僅重跑一次後嚴格 50/50 通過。最終證據與公開文件獨立覆核皆為 0 High／Medium／Low。**
> [本次滾輪修正](docs/checkpoints/2026-09-28/research/wheel-events/README.md) · [按鍵盤點](docs/checkpoints/2026-09-28/research/key-map/README.md) · [結束鍵](docs/checkpoints/2026-09-28/research/end-keys/README.md) · [滑過事件](docs/checkpoints/2026-09-28/research/hover-events/README.md) · [自動輸入](docs/checkpoints/2026-09-28/research/auto-input/README.md) · [CG 翻轉](docs/checkpoints/2026-09-28/research/cg-deform/README.md) · [構築命令](docs/checkpoints/2026-09-28/research/construction-shapes/README.md) · [物品框](docs/checkpoints/2026-09-28/research/cg-scale-copy/README.md) · [成就橫幅](docs/checkpoints/2026-09-28/research/gradation-text/README.md) · [按鈕文字](docs/checkpoints/2026-09-28/research/button-text/README.md) · [X／Y 旋轉](docs/checkpoints/2026-09-28/research/parts-rotate-xy/README.md) · [技能欄](docs/checkpoints/2026-09-28/research/activity-release/README.md) · [捲軸把手](docs/checkpoints/2026-09-28/research/scrollbar-knob/README.md) · [APEG 影像解碼](docs/checkpoints/2026-09-28/research/apeg-video/README.md)。**仍不能當作完整遊戲使用。** 原版物品／回顧／據點及環節選擇有局部並排證據，不代表跨語系全圖相同；人工足額商店扣款成立，設備效果與重載持久化未驗。原版複合 owner／內部焦點與候選 scope、既有右鍵隱藏與畫面差異、SJIS、音訊及長時穩定仍有限制。G5 發布核對後依序做 G1、子畫面 scope／G4、G7、G8，再續試玩。詳見[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
> [最新進度](docs/checkpoints/2026-09-28/STATUS.md) · [9/26 交接與下一步](docs/checkpoints/2026-09-26/STATUS.md) · [重建／重跑](docs/checkpoints/2026-09-26/REPRODUCE.md) · [完整研究報告](docs/checkpoints/2026-09-26/outputs/xsystem4-原版逆向與原生路線研究-2026-09-20.md) · [API對照地圖](docs/checkpoints/2026-09-26/outputs/原生研究-API地圖-2026-09-20.html)

xsystem4 is an implementation of AliceSoft's System 4 game engine for unix-like
operating systems.

Compatiblity
------------

See the [game compatibility table](game_compatibility.md) for a list of games
that can be played on xsystem4.

Building
--------

First install the dependencies (corresponding Debian package in parentheses):

* chibi-scheme [optional, for debugger]
* bison (bison)
* cglm (libcglm-dev) [optional, fetched by meson if not available]
* flex (flex)
* freetype (libfreetype-dev)
* glew (libglew-dev)
* meson (meson)
* libffi (libffi-dev)
* libpng (libpng-dev)
* libsndfile (libsndfile-dev)
* libturbojpeg (libturbojpeg0-dev)
* libwebp (libwebp-dev)
* SDL2 (libsdl2-dev)
* zlib (zlib1g-dev)

Then fetch the git submodules,

    git submodule init
    git submodule update

(Alternatively, pass `--recurse-submodules` when cloning this repository)

Then build the xsystem4 executable with meson,

    mkdir build
    meson build
    ninja -C build
    
Finally install it to your system (optional),

    ninja -C build install

### Windows

xsystem4 can be built on Windows using MSYS2.

First install MSYS2, and then open the MINGW64 shell and run the following command,

    pacman -S make flex bison \
        mingw-w64-x86_64-gcc \
        mingw-w64-x86_64-meson \
        mingw-w64-x86_64-pkg-config \
        mingw-w64-x86_64-SDL2 \
        mingw-w64-x86_64-freetype \
        mingw-w64-x86_64-libjpeg-turbo \
        mingw-w64-x86_64-libwebp \
        mingw-w64-x86_64-libsndfile \
        mingw-w64-x86_64-glew \
        mingw-w64-x86_64-nasm \
        mingw-w64-x86_64-diffutils

To build with FFmpeg support, you must compile FFmpeg as a static library:

    git clone https://github.com/FFmpeg/FFmpeg.git
    cd FFmpeg
    git checkout n7.1.2
    ./configure --disable-everything \
        --enable-decoder=mpegvideo \
        --enable-decoder=mpeg1video \
        --enable-decoder=mpeg2video \
        --enable-decoder=mp2 \
        --enable-parser=mpegaudio \
        --enable-parser=mpegvideo \
        --enable-demuxer=mpegps \
        --enable-demuxer=mpegts \
        --enable-demuxer=mpegtsraw \
        --enable-demuxer=mpegvideo \
        --enable-decoder=vc1 \
        --enable-decoder=wmapro \
        --enable-parser=vc1 \
        --enable-hwaccel=vc1_d3d11va \
        --enable-hwaccel=vc1_d3d11va2 \
        --enable-hwaccel=vc1_dxva2 \
        --enable-demuxer=asf \
        --enable-protocol=file \
        --enable-filter=scale \
        --enable-static \
        --disable-shared \
        --extra-libs=-static \
        --extra-cflags=--static
    make
    make install
    cd ..
    export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"

Then build the xsystem4 executable with meson,

    mkdir build
    meson build
    ninja -C build

To create a portable executable, it is neccessary to copy some DLLs into the same directory as xsystem4.exe.
You can determine the required DLLs with the following command,

    ldd build/src/xsystem4.exe | grep mingw64

The `fonts` and `shaders` directories must also be shipped together with xsystem4.exe.

Installing
----------

If building from source, run:

    ninja -C build install

to install xsystem4 on your system.

### Windows

The recommended way to install on Windows is to copy the xsystem4 directory
into the game directory. E.g. if Sengoku Rance is installed at
`C:\Games\AliceSoft\Sengoku Rance`, then your filesystem should look something
like this:

    C:\
        Games\
            AliceSoft\
                Sengoku Rance\
                    System40.ini
                    xsystem4\
                        xsystem4.exe
                        ...
                    ...

Running
-------

You can run a game by passing the path to its game directory to the xsystem4
executable,

    build/src/xsystem4 /path/to/game_directory

Alternatively, run xsystem4 from within the game directory,

    cd /path/to/game_directory
    xsystem4

### Windows

If you've installed xsystem4 into the game directory as described above, simply
double-click `xsystem4.exe` to run the game.
