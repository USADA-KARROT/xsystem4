xsystem4
========

> **最新狀態（2026-10-25）：v14 聲音增益程式 `ebda614` 已推送，三方雜湊一致，parent `81fccb3`，libsys4 `247f544`。提交前／後預設與 GBK 各 117 模式 PASS、sanitizer 0；最終 parent before-check 為 7/9 案語義失敗、資料自檢與舊路防守案通過。提交前／後九路線日誌與操作門檻通過；片頭改用新判準（依整趟擬合的相位逐張選格、逐像素；不驗聲音端），提交後首輪通過，舊判準 49/50（t311）照記；同一支 binary 的第二、三趟也各自通過，三趟的組層級判準通過（起點相對載入結束平均 −3.7 ms，在 ±15 ms 內）。片頭起點的變動主要來自載入時片頭檔在不在頁面快取，同樣快取條件下 G1 與 G2 沒有可分辨的差別（熱快取相同到 0.1 ms，冷快取分布重疊）。本組的探針審查、實作審查、修正後複審與推送前兩個覆核都沒有未解的 High／Medium（片頭判準工具的複審留下一個 Medium，以交齊同一支 binary 的全部片頭趟處理）。**
> [本次聲音增益](docs/checkpoints/2026-09-28/research/sound-gain/README.md) · [聲音群組](docs/checkpoints/2026-09-28/research/sound-groups/README.md) · [滾輪](docs/checkpoints/2026-09-28/research/wheel-events/README.md) · [按鍵盤點](docs/checkpoints/2026-09-28/research/key-map/README.md) · [結束鍵](docs/checkpoints/2026-09-28/research/end-keys/README.md) · [滑過事件](docs/checkpoints/2026-09-28/research/hover-events/README.md) · [自動輸入](docs/checkpoints/2026-09-28/research/auto-input/README.md) · [CG 翻轉](docs/checkpoints/2026-09-28/research/cg-deform/README.md) · [構築命令](docs/checkpoints/2026-09-28/research/construction-shapes/README.md) · [物品框](docs/checkpoints/2026-09-28/research/cg-scale-copy/README.md) · [成就橫幅](docs/checkpoints/2026-09-28/research/gradation-text/README.md) · [按鈕文字](docs/checkpoints/2026-09-28/research/button-text/README.md) · [X／Y 旋轉](docs/checkpoints/2026-09-28/research/parts-rotate-xy/README.md) · [技能欄](docs/checkpoints/2026-09-28/research/activity-release/README.md) · [捲軸把手](docs/checkpoints/2026-09-28/research/scrollbar-knob/README.md) · [APEG 影像解碼](docs/checkpoints/2026-09-28/research/apeg-video/README.md)。**仍不能當作完整遊戲使用。** 聲音增益只在聲音群組啟用時（v14、GBK 且主 EX 有群組表）生效：每個聲音乘主 EX 的內部音量，預設下 BGM：語音：音效在移植版錄音量到約 0.24：0.64：0.60，等於依反組譯與 EX 推算的原版（BGM 比 G1 小約 8.5 dB）；片頭聲音照原版掛在 SoundGroup 1 並乘影片的內部音量（預設約 0.38，跟總控與 BGM、不跟音效）；GetLengthFromFile 回毫秒。提交說明有兩句比實作寬（夾限、影片鍵），以研究文件為準；兩處程式註解與語義審查 A-L1／A-L2 排到下一個程式提交。英文原版未對照、原版沒有錄音，系統音效該乘哪個內部音量鍵沒追（G11），plmpeg 後端沒有建置；戰國蘭斯（舊路）只在螢幕鎖定下經墊片跑，對白、點擊與 ADV 段截圖和 G1 建置相同、BGM 增益照舊 1.0。G1 有一個回歸要先修（讀程式發現，未實機重現）：一次性的聲音自然播完後沒有倒回開頭。原版複合 owner／內部焦點與候選 scope、既有右鍵隱藏與畫面差異、SJIS 及長時穩定仍有限制。接著依序做 G1b（聲音播完不倒回開頭）、子畫面輸入範圍／多送的 MouseEnter、G4、G7（已在進行）、G8，其餘（G11 等）與試玩照原順序。詳見[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
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
