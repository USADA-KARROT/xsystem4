xsystem4
========

> **最新狀態（2026-11-09）：G15 補上 v14 的構築命令 103、107、121（圓的 AMap 與 AlphaBlend 版本、扇形，都套在矩形裡）：根據地成員的狀態畫面與人材的操作畫面下方，「第幾個人」的位置列（`PositionView`）畫出兩端的半圓、每個位置的圓點與目前位置上的黃色游標；修前只剩中間的半透明底，使用者紀錄裡有 unknown type 103／107／121 的警告。程式提交 `44283e3`，父提交 `6c0a2f3`，libsys4 固定 `247f544`。只在 v14 的 AIN 綁定；三個命令不讀字串，不分字集；新增操作型別 32–34，XPE 存檔版本 13 → 14；尺寸上限照 xsystem4 自己的（2048），奇數尺寸照原版畫。新模式 `construction-in-rect` 13 案（137 → 138 模式）；修前兩種字集恰好 IR1–IR8、IR12、IR13 失敗。突變 200 個抓到 197 個（列舉的 2 個判等價，審查另寫的 1 個存活）。提交前（在 `8f9cd6d` 上，136 模式）與提交後（138 模式）預設與 GBK 都通過、sanitizer 0；九條路線提交前對 G10a、提交後對 G14 修正的基準對白相同、錯誤 0、桌面游標不變（路線都走不到這三個命令）；片頭三趟新判準各 50/50、組層級通過（提交前平均 +1.1 毫秒、提交後 −1.6 毫秒）。實機（存檔複本、固定種子）修後位置列畫出來，游標隨換人移到下一點，警告每趟 8 次 → 0 次。原版畫面沒有拍到（Wine 的工具偵測到螢幕鎖定），原版的期望只來自反組譯與 AIN。**
> [構築命令 103、107、121 研究](docs/checkpoints/2026-09-28/research/construction-in-rect/README.md) · [前一組 HLL 呼叫交出的參照](docs/checkpoints/2026-09-28/research/hll-param-handover/README.md)。原版依據是靜態反組譯（跳表 `0x4fb7d8`：103 `0x507790`、107 `0x507f80` 照命令 101 取 W、H 的較大者當直徑，121 `0x5092a0` → `0x5b0760` 是 rx = W/2、ry = H/2 的內切橢圓扇形）。和原版仍不同的有：103 畫在沒有 alpha 的表面、107 一格都沒寫到時，原版這一步失敗、後面的步驟不做，移植版只跳過這一步；尺寸超過 2048 原版照畫、移植版不畫；旋轉不是 0 時移植版不畫（原版把 360 的倍數畫成 0、其他角度照轉）。102、106 的圓掃描與 122 的扇形掃描改成和新命令共用的常式，像素不變（探針守著）。仍不能當作完整遊戲使用。下一步：U2（字串編碼判定收緊、`--encoding` 選項、上游的 6 號馬賽克原點修正；已完成，等提交）；之後依序 G10b → G12a → 每層取樣與取消旗標 → G1c → G12b → 從第 8 天以後試玩。
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
