xsystem4
========

> **最新狀態（2026-11-01）：G8 v14 的滑鼠三鍵、拖曳通知與按鍵事件 15–18，程式提交 `2d76d52`，parent `3f20f3e`，libsys4 `247f544`。v14 的右鍵與中鍵照原版換焦點，放開時送全畫面與焦點元件的 MouseClick（鍵碼 2、4）；全畫面與焦點元件送 KeyTrigger／KeyDown／KeyPress／KeyUp（固定 80 鍵、重複 500／80 毫秒）；拖曳送 DragBegin、Draging（五個整數）與 DragEnd，拖曳中按另一顆鍵照原版取消；橫桿軌道的按鍵與點擊轉給外層元件；SDL 的左右 Shift、Ctrl 與 Return／數字鍵盤 Enter 分開記再合併，數字鍵盤 Enter 當 Return。新邏輯只在 v14，不分字集。新探針 `key-events` 80 案、`key-scrollbar` 8 案、`key-consumer` 2 案；修前（parent）`key-events` 78 案、`key-scrollbar` 8 案失敗，`input-model` 只有 IM4、`auto-input` 只有 AU6 失敗。提交前預設與 GBK 各 126 模式通過、sanitizer 0；九路線對 P0 基準對白相同、錯誤 0、桌面游標不變，片頭三趟新判準均 50/50、組平均 −1.0 毫秒；提交後兩組各 126 模式、九路線、片頭三趟及組層級均通過，片頭組平均 +6.0 毫秒。**
> [按鍵事件研究](docs/checkpoints/2026-09-28/research/key-events/README.md) · [前一組活動容量](docs/checkpoints/2026-09-28/research/activity-capacity/README.md)。原版依據來自靜態反組譯與 AIN；實測只在移植版：設定頁右鍵、Esc、空白鍵各只關一次，道具頁一次右鍵返回據點，戰鬥中右鍵回技能選擇，頁尾鈕按住時下沉，拖曳中按中鍵後把手照樣跟著游標。戰鬥右鍵、拖曳中取消、設定頁 Esc／空白鍵關閉沒有原版畫面；被取消的左來源跨過輸入範圍之後的按下外觀、切換作用中層時保留的焦點收到的延遲點擊和原版仍不同（推論），對話右鍵隱藏時留下的按鈕是既有差異。G8 也帶進一個已知錯誤：按住左鍵時切換作用中層、焦點又在另一層被移走或清掉時，元素的按鍵表會殘留左鍵，之後右鍵或中鍵按它會送出左鍵的點擊（觸發路徑很窄，例行路線沒有走到），排在下一組 G8b。仍不能當作完整遊戲使用。接著依序處理 U1、G8b、G9、G10、G12、U2、G1c。
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
