xsystem4
========

> **最新狀態（2026-11-01）：U1 上游的四個小修，程式提交 `e5d8963`，parent `814a83c`，libsys4 `247f544`。多行文字經存檔讀回時補回換行、讀出的字串也釋放；影片聲音每格交給混音器的樣本數改用那一格實際的樣本數（片頭最後一格原本多送 1023 個樣本）；parts 的動畫名稱、只為確認存在而載入的 CG 與讀檔時讀出的 CG 名稱用完就釋放；Asin／Acos 在 v14 照原版回傳弧度。多行文字與影片聲音不限版本與字集，Asin／Acos 的 v15 以上跟 v14、v13 以下保留度。新探針 `save-text-lines` 8 案、`movie-audio-length` 6 案、`parts-leaks` 7 案、`math-inverse-trig` 7 案；修前（parent）28 案中 17 案失敗、兩種字集相同。提交前預設與 GBK 各 130 模式通過、sanitizer 0；九路線對 G8 基準對白相同、錯誤 0、桌面游標不變，片頭三趟新判準均 50/50、組平均 −2.3 毫秒；提交後兩組各 130 模式、九路線、片頭三趟及組層級均通過，片頭組平均 −2.6 毫秒。**
> [上游小修研究](docs/checkpoints/2026-09-28/research/upstream-fixes/README.md) · [前一組按鍵事件](docs/checkpoints/2026-09-28/research/key-events/README.md)。原版依據只有 Asin／Acos 的 v14（Math 的分派 `0x4c6520` 不換算單位；被呼叫的兩個函式是 asinf／acosf，這一點只依形狀推論），另外三項修的是移植版自己的錯。片頭錄音：修前在片頭結束後多送 1023 個樣本（約 −101 dB rms，等於最後一格緩衝區在實際樣本之外的內容），修後是 0；英文版戰國蘭斯的兩部影片每格的長度修前修後相同，冒煙 300 秒錯誤 0。多行文字讀檔與讀檔時的 CG 名稱在例行路線上沒有走到，只靠探針；兩款遊戲都不呼叫 Asin、Acos。v13 以下保留度沒有原版依據；`_parts_cg_set` 的既有洩漏、v14 以前 gauge 的 CG 留到之後。仍不能當作完整遊戲使用。接著依序處理 G8b、G9、G10、G12、U2、G1c。
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
