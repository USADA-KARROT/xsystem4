xsystem4
========

> **最新狀態（2026-11-02）：G9 v14 信息窗口的亮度與不透明度生效、設定第 3 頁的預覽有樣本文字、文字樣式的字型尺寸在改字型或字級後重查，程式提交 `c3e7649`，parent `42aa793`，libsys4 `247f544`。`UpdateComponent` 的最後兩個參數（腳本傳 `g_AFConfig` 的成員 14 亮度與成員 15 不透明度）原本被丟掉，設定第 3 頁的兩條橫桿會改會存、畫面卻不變；現在照原版 `0x4f2660` 只改訊息框背景的乘色與 alpha，乘在視窗本身的值上，文字與鍵待標記不變，兩條都是 1.0 時畫面逐位元不變。訊息框的 pactex 載入讀「文本」（SJIS「テキスト」），預覽出現三行樣本文字，ADV 的四個訊息框也照原版帶它。`text_style_font_size` 在 v14 每次依字型與字級重查（防禦性修正）。另照原版語義綁了 EffectLink 的兩個函式與 `UpdateMatrix`（移植策略，這個遊戲在量得到的範圍內不呼叫）。新邏輯只在 v14，不分字集。新模式 `msgwin-rates` 16 案、`msgwin-sample` 6 案、`font-cache` 7 案（130 → 133 模式）；修前（parent）29 案中 20 案失敗、兩種字集相同。突變 104 個中 102 個被抓到。提交前預設與 GBK 各 133 模式通過、sanitizer 0；九路線對 G8b 基準對白相同、錯誤 0、桌面游標不變，片頭三趟新判準均 50/50、組平均 +5.3 毫秒；實機拉低兩條之後預覽與對白框的背景都變了；提交後兩組各 133 模式、九路線、片頭三趟及組層級均通過，片頭組平均 −2.6 毫秒。原版不同值的畫面沒有拍到，比率的期望值來自反組譯。**
> [信息窗口設定研究](docs/checkpoints/2026-09-28/research/message-window-config/README.md) · [拖放範圍](docs/checkpoints/2026-09-28/research/drag-scope/README.md) · [前一組切換作用中層](docs/checkpoints/2026-09-28/research/layer-switch/README.md)。原版依據是靜態反組譯（`0x57bb82` → `0x53c490` 不看 show 就把兩個比率寫進更新上下文；訊息框的更新 `0x4f2660` 只改私有背景的乘色與 alpha；pactex 載入在 `0x4f0d0f` 讀「文本」），原版不同值的畫面沒有拍到（螢幕鎖著），比率的期望值全部來自反組譯。和原版仍不同的有：基底色與目標色是常數（白與黑，這個遊戲五個訊息框與 EX 的值）、背景和視窗 alpha 的合成用浮點（推論最多差一級）、比率全域只有一份、預覽的信息番號是 −1、EffectLink 的 pactex 鍵與存讀檔沒做、字型重查只在 v14；範圍外的 `SetMessageWindowActive(false)` 會藏起視窗、載入時無條件設 ShowLink，列進待辦。拖放畫面的唯讀研究一起公開：拖放與滑動不擋遊玩，遊戲本體只用左鍵的 7、8、10，G12 縮成 G12a（讀 `[ドラッグ]`、驗消費端、編隊與人材清單並排）與 G12b（滑動）。仍不能當作完整遊戲使用。接著依序：G10 → U2 → G1c → G12a → 每層取樣與取消旗標 → G12b → 從第 6 天以後試玩。
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
