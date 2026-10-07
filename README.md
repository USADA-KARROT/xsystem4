xsystem4
========

> **最新狀態（2026-10-08）：`884b638` 讓 Array HLL 把元素是 option 的陣列當成兩槽或三槽的元素（型別參數 `0x20002`／`0x30002`／`0x30003`；原版 `0x66d470`、`0x653420`、`0x647470`）：春銷的顧客第一次成交、出現結算畫面並入帳，進地城不再當機，地圖可以移動，並進入第一場戰鬥的指令選擇。前一天的五組：`3eb1b36` pactex 的 `點擊許可`、`6687656` 文字與字型數字的尺寸、`b05ff2d` 自由盒依原點位移、`7e0b781` 構築部件載入時建構、`8a1dcdc` 版面盒排版。預設／GBK 各68模式 PASS、sanitizer0；新探針修前14/17失敗、修後17/17通過。正常GUI150.361秒、MSG88、assert／overflow0。**
> [本組研究與驗證](docs/checkpoints/2026-09-28/research/option-array/README.md) · [點擊許可](docs/checkpoints/2026-09-28/research/gui-visual/click-permission.md) · [文字尺寸](docs/checkpoints/2026-09-28/research/gui-visual/text-size.md) · [自由盒](docs/checkpoints/2026-09-28/research/gui-visual/free-box.md) · [灰底](docs/checkpoints/2026-09-28/research/gui-visual/construction.md) · [排版](docs/checkpoints/2026-09-28/research/gui-visual/layout-box.md)。**還不能當成遊戲來玩，但已能以自動路線實機從開場走到第一場戰鬥**：春銷成交與結算、第 2 天的據點、地城選擇、地圖移動、戰鬥的指令選擇。已知卡點：戰鬥畫面與對話框有大塊不透明的黑（面板的半透明色，下一組）、戰鬥中看不到角色與敵人、設定與劇情回顧（controller 只接受 -1）、讀檔（`system.Reset` 是空殼）、滑鼠只有左鍵點擊。戰鬥的後續、道具、商店都還沒有實測。見[交接](docs/checkpoints/2026-09-28/HANDOFF.md)最上節。長時間穩定性尚未驗證。
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
