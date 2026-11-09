xsystem4
========

> **最新狀態（2026-11-09）：U2 收緊字串編碼的判定、加上 `--encoding`，6 號馬賽克照原版：沒有指定編碼時，只有嚴格判定成立才把遊戲當成 GB18030（兩層開關一起開），寬鬆分數只記錄，英文版戰國蘭斯（AIN v5）不再被誤判、回到 Shift-JIS（v13 稽核第 1 條）；`--encoding`（`sjis` 或 `gbk`）與 `.xsys4rc` 的 `encoding` 鍵讓兩層一起關或一起開，命令列優先（照上游的想法自己重寫，不搬上游 libsys4 的 GBK 語義）；拿掉除錯用的 AINCHECK 一行。6 號效果（交叉淡入的馬賽克）的格子大小（奇數、最大 97）、原點（畫面中心取整）與取樣點（每格第一個像素）照英文版戰國蘭斯效果外掛的靜態反組譯。程式提交 `6e4e673`，父提交 `672f0d0`，libsys4 固定 `247f544`。多娜多娜的表嚴格判定成立（邊界分數 92、SJIS 不合法而 GBK 合法的字串 1419 個），兩層開關與存檔資料夾不變，記錄只少 AINCHECK 一行；它沒有會畫效果的函式庫，著色器不載入。新模式 `encoding-select` 40 案、`mosaic-effect` 19 案（138 → 140 模式）；修前兩種字集恰好 30 案與 16 案失敗。突變 93 個全部抓到。提交前（在 `8f9cd6d` 上，137 模式）與提交後（140 模式）預設與 GBK 都通過、sanitizer 0；九條路線提交前對 G10a 的基準全過；提交後對 G15 的基準八條通過，戰鬥路線落在一個和視窗輸入焦點有關、改動之前就出現過的分群（樣本小，不能排除改動讓它更常出現），照規則不過，以人工判定放行；片頭三趟新判準各 50/50、組層級通過（提交後平均 +0.8 毫秒）。英文版戰國蘭斯冒煙：對白與點擊不變、誤判的三行記錄消失，走過兩次 6 號效果沒有錯誤；馬賽克的畫面沒有拍到，原版畫面也沒有。**
> [字串編碼與 6 號馬賽克研究](docs/checkpoints/2026-09-28/research/encoding-select/README.md) · [前一組構築命令 103、107、121](docs/checkpoints/2026-09-28/research/construction-in-rect/README.md)。原版依據是靜態反組譯（`GLEffectCopy.dll`：6 號 → `0x10005940` 算格子大小、`0x10002560` 畫一張畫面；`Sact2.dll` 只在 view mode 0 呼叫它）。和原版仍不同的有：著色器只拿到進度，奇數長度與少數時刻格子大小差 2；交叉淡入的顏色是 GPU 的 mix（模型差到 2 或 3 級）；奇數畫面的最後一欄或一列原版不畫；view mode 1 與其他遊戲的外掛沒有讀。仍不能當作完整遊戲使用。下一步：進行中 G1c、G12a；之後依序 G10b → 每層取樣與取消旗標 → G12b → 從第 8 天以後試玩。
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
