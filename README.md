xsystem4
========

> **最新狀態（2026-11-02）：G8b v14 切換作用中層時，每層保留自己的三個來源槽、左鍵按下標記與焦點，程式提交 `d698d6a`，parent `3228ba8`，libsys4 `247f544`。修好 G8 留下的殘留左鍵位元（切層後焦點被別層的按下移走、或被那裡的 BeginInput 清掉之後，右鍵或中鍵按到那個元素時送左鍵的 KeyUp 與 MouseClick，左鍵按到時沒有 Trigger）：別層的按下不再搶走舊層的焦點、不送它 LostFocus；切回時舊層的焦點收到延遲的 KeyUp 與 MouseClick，`GetActiveParts` 立刻是它；BeginInput 與 EndInput 靜默重設每一層槽裡的元件；切回時另一顆按著、而那層離開時記的是放開的滑鼠鍵，取消保留的槽。取樣的按鍵、全畫面鍵表、儲存的位置與取消抑制仍共用（移植策略）；新邏輯只在 v14，不分字集。`key-events` 80 → 123 案、`layer-input` 12 → 13 案；修前（parent）新的 44 案中 32 案失敗、兩種字集相同（其中 KE112 只在釘移植版的那一格失敗）。突變 66 個全殺。提交前預設與 GBK 各 130 模式通過、sanitizer 0；九路線對 U1 基準對白相同、錯誤 0、桌面游標不變，片頭三趟新判準均 50/50、組平均 −2.4 毫秒；提交後兩組各 130 模式、九路線、片頭三趟及組層級均通過，片頭組平均 −0.2 毫秒。**
> [切換作用中層研究](docs/checkpoints/2026-09-28/research/layer-switch/README.md) · [前一組上游小修](docs/checkpoints/2026-09-28/research/upstream-fixes/README.md) · [按鍵事件（G8）](docs/checkpoints/2026-09-28/research/key-events/README.md)。原版依據是靜態反組譯（只有作用中層的管理器跑 `0x53e690`／`0x53c5b0`、焦點只在自己那層的清單找 `0x545c70`、BeginInput／EndInput 對每個管理器靜默重設槽裡的元件 `0x53e8b0` → `0x5461a0` → `0x577af0`、LostFocus 的順序 `0x53e830`），原版的期望全部是推論、沒有原版畫面。和原版仍不同、推論的有：按住跨過換層的鍵對接手那層不是新的按下（只有保留的焦點看到按下）、切回時不送第二次全畫面 KeyUp／Click、BeginInput 時全畫面鍵表只放開一次、取消抑制不分層、改掛焦點的幾個時點；這些排成新的一組「每層取樣與取消旗標」。例行路線沒有走到要修的情境（只有戰鬥路線一次按住期間換層，形狀不同、修前就有），正確性只靠探針。仍不能當作完整遊戲使用。接著先查拖放畫面有沒有點擊替代、從第 6 天以後試玩，再依序處理 G9、G10、G12、U2、G1c。
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
