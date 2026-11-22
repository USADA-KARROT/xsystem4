xsystem4
========

> **最新狀態（2026-11-22）：聲音停止型淡出被打斷（v14 GBK）的提交前驗收、提交後驗收與推送前覆核完成；聽感沒有人聽。**AIN v14、GBK 的遊戲在停止型淡出（帶停止旗標、淡完要停聲的淡變）還沒結束時又收到 `KiwiSoundEngine.Fade`，移植版直接換掉舊的淡變、聲道繼續播；原版（只讀一個 v14 GBK 樣本、靜態讀法，沒有執行、沒有錄音）先停聲再換上新的淡變，時間小於 1 即時套目標，淡變照計時器跑、不管聲道有沒有在播。本組改引擎 5 個檔：照原版處理打斷、時間 0 與負的時間；移植版沒有淡變執行緒，改成打斷停止型淡出的定時淡變與之後那個聲道上的淡變、在沒在播的聲道上發起的淡變、停止或播完時還在進行的淡變照牆鐘（SDL 的毫秒計數）結算，其餘 v14 淡變仍照混音幀推進；讀取函式只在有牆鐘淡變時拿音訊裝置的鎖（播放前就下的 v14 淡入期間會拿鎖，背景音樂淡入期間的幀時間沒有量到退步（效能紀錄只有每 5 秒一筆摘要、趟數少），所以維持）。**只對 AIN v14 且 GBK 的遊戲生效**，其他遊戲與既有入口維持舊行為；`Play` 不重設音量、照混音幀推進的淡變上 `StopFade` 跳到終值等和原版的差異照實列在研究。程式 `6b42512`（父提交 `31a6dde`，8 個檔、+1135／−26，模式 157 → 158）：新模式 `sound-fade-interrupt` 13 案，父提交的引擎上 12 案失敗、修後 13／13，九個相鄰模式逐案相同；突變 73 列命中 60、接受的存活 13；預設與 GBK 各 158 模式通過、sanitizer 0。提交前的視窗驗收用的是私人來源副本上建的另一支執行檔（引擎原始碼和提交逐檔相同，差異只有版本字串與路徑字串及連帶的位址），提交後已用提交上重建的執行檔重跑：片頭三趟與九條路線照既有判準通過，音訊警告 0，停止型淡出被打斷的序列和修改前相同；實機路線沒有走到的情境只有探針涵蓋。推送前覆核的兩位覆核者（證據、公開內容）都判程式提交可以推送（對程式提交沒有 High 或 Medium），對文件的意見（Medium 1、Low 7）已在這次文件改正。程式推送的三方核對記在[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
> **給玩家**：換背景音樂或音效、前一個淡出還沒結束就來了新的淡變時，現在照原版處理；自動測試與實機的自動驗收都通過、換音樂時畫面沒有量到變卡；**沒有人實際聽過**，聽到前一首沒停、聲音突然中斷、音量不對請回報。詳見[聲音停止型淡出研究](docs/checkpoints/2026-09-28/research/sound-fade-interrupt/README.md)與[審查](docs/checkpoints/2026-09-28/research/sound-fade-interrupt/review.md)。
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
