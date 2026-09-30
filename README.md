xsystem4
========

> **2026-09-30 macOS／多娜多娜 CN：第三輪審查的項目已修正（`80db27d`，本機 commit，尚未推送）：pactex 的 `オン指針透過` 照原版讀入，懸停與點擊共用原版的輸入目標，標題角色、人材卡文字、環節卡小圖不再擋住後方按鈕；刪除依指令數把解構子列入黑名單的 fork 邏輯，人材 → 返回 → 下一步不再斷言；54 個 headless 模式在預設與強制 GBK 下皆通過，150 秒 GUI 88 筆對白、無斷言。第二輪審查的項目已修正（`6582e38`，本機 commit，尚未推送）：v14 delegate 照原版不再持有目標物件，場景物件會釋放，按下新遊戲後標題按鈕隨圖層消失、鑑賞模式返回不再 VM_ERROR、據點背景出現並可經階段選擇進入春銷；`A_REF` 照原版複製陣列，標題構圖不再被就地反轉；點擊派送與懸停規則一致；53 個 headless 模式在預設與強制 GBK 下皆通過，150 秒 GUI 88 筆對白、無斷言，峰值 RSS 607→506 MB。據點與流暢度審查的項目已修正（`fb28975`，本機 commit，尚未推送）：停用按鈕不再送出點擊、偵測元件的點擊判定改用預設狀態、兩槽 Array 元素的回傳與步幅（頁首與橫幅右偏 73 px 的根因，motion 也改用各自的長度）、矩形與構築部件的型別、結束時等待截圖寫完；52 個 headless 模式在預設與強制 GBK 下皆通過，150 秒 GUI 88 筆對白、無斷言與堆疊溢位。畫面照原版 `0x4676f0`／`0x4c5450` 限速並每幀只呈現一次（`44964f9`，本機 commit，尚未推送）：元件時間不再每幀推進兩次，截圖改在背景執行緒寫檔；51 個 headless 模式在預設與強制 GBK 下皆通過，150 秒 GUI 一般遊玩由平均約 320 fps（浮動）變成固定 58.7 fps、遊戲時間倍率 0.74→0.97，測試模式超過 50 ms 的長幀 40–42→2–3，88 筆對白、無斷言與堆疊溢位。據點畫面的底列、「下一步」、日數與金錢、貼紙、橫幅與教學覆蓋層照原版建立（`6d39915`，本機 commit，尚未推送）：父元件立即掛上、pactex 依原版型別表，另實作 SetButtonEnable；50 個 headless 模式在預設與強制 GBK 下皆通過，150／220 秒 GUI 88 筆對白、無斷言與堆疊溢位，「下一步」可推進到階段選擇（之後因教學場景物件未釋放而停止）。翻轉旗標改為作用在整棵元件樹（`0ab8476`，本機 commit，尚未推送）：照原版 `0x535260`／`0x4e6d80` 沿父元件鏈 XOR、以錨點為軸鏡像方框與子元件位置，`AdvStand@Move` 跨側與戰鬥的 rect 翻轉因此生效；49 個 headless 模式在預設與強制 GBK 下皆通過，兩次 150 秒 GUI 88 筆對白、無斷言與堆疊溢位。delegate 呼叫的參數複製（`2005274`）同為本機 commit：改照原版 `0x66dce0`／`0x657430` 一格堆疊對一個參數變數。CN 文字的 GDI 字格排版（`9f81bd9`）、立繪與名牌不退場的 use-after-free（`9e30c0f`、`4d52a87`）、GBK 字元規則、左側翻轉、缺字 fallback 與存讀檔持久化到 `6421e6e` 已推送。**
> 從讀檔畫面讀一般存檔仍需 system.Reset；記憶體成長（STRUCT 參數多加參照等候選）以及長時間穩定性仍待處理，尚非穩定可玩版。
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
