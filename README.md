xsystem4
========

> **最新狀態（2026-11-08）：G13 v14 訊息框的連動隱藏傳給整個子樹：右鍵隱藏對話框時，頭像、名牌、左下與右下的按鈕、選單鈕與 NEXT 一起藏，隱藏中點不到；pactex 載入與 `SetMessageWindowActive` 不再無條件設連動，`SetMessageWindowActive` 也不再改 show。程式提交 `4b681b2`，父提交 `8f9cd6d`，libsys4 固定 `247f544`。新邏輯只在 v14、不分字集（英文版執行檔在相關函式除位址與鍵的長度外指令相同）。新模式 `msgwin-showlink` 14 案（135 → 136 模式）；修前兩種字集恰好 SL1–SL9 失敗。突變 75 個中 68 個被抓到、7 個判等價。提交前與提交後預設與 GBK 各 136 模式通過、sanitizer 0；九條路線對 G10a 的基準對白相同、錯誤 0、桌面游標不變；片頭三趟新判準各 50/50、組層級通過（提交前平均 +6.3 毫秒、提交後 −6.9 毫秒）。實機右鍵隱藏後，和英文版原版的隱藏畫面只差兩個下角的 88 個像素；隱藏中點選單鈕只恢復顯示，和原版相同。對話中打開劇情回顧時一起藏是推論，沒有原版畫面。**
> [訊息框連動隱藏研究](docs/checkpoints/2026-09-28/research/message-window-showlink/README.md) · [前一組游標設定與螢幕保護](docs/checkpoints/2026-09-28/research/cursor-settings/README.md)。原版依據是靜態反組譯（`0x535260` 把連動併進合成 show、傳給整個子樹；輸入只拿合成 show 為真的 parts；pactex 載入與 `SetMessageWindowActive` 都不設連動），右鍵隱藏和英文版原版並排相同。和原版仍不同的有：輸入的時序（移植版同一次更新、原版推定晚一次）、`SetMessageWindowActive` 的查找與作用中旗標、pactex 的連動鍵沒讀、核取方塊的顯示連動（`0x537f10`）沒做。仍不能當作完整遊戲使用。進行中：G14 修正、G15、U2；之後依序 G10b → G12a → 每層取樣與取消旗標 → G1c → G12b → 從第 8 天以後試玩。
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
