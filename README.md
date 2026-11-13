xsystem4
========

> **最新狀態（2026-11-13）：B1 的提交前完整實機驗收與提交後驗收完成。**v14 的陣列存入 struct 值的元素時（Add、PushBack、Insert、Fill、帶值的 Realloc），照原版由呼叫端新開元素、再由元素設定函式逐成員指派（Fill 與帶值 Realloc 的新尾巴對既有元素就地指派），並照原版 struct 指派的前置與收尾，把綁在來源物件上的 delegate 項目帶到複本上；v14 之前、不是 struct 值的陣列，以及含本程式不複製的成員型別的 struct 不變。修前移植版只加參照、元素和值共用同一頁，戰鬥的動作格快取因此被改寫，同型敵人第二次以後被擊倒時取不到動作格、擊倒演出被略過。程式 `b0261a8`（父提交 `bbb612b`，模式 141 → 142）：修前兩種組態 `array-struct-copy` 72 案中 53 案、`hll-param-handover` 只有 HP1–HP3 如預期失敗；提交前最後的來源與程式提交上，預設與 GBK 各 142 模式通過、sanitizer 0；九條路線裡八條照既有判準和基準相符，片頭那條只差動態 parts 編號，把編號轉回基準的編號空間後，既有的路線判準與片頭相位判準原封不動通過（提交前三趟組平均 −4.9 ms、提交後三趟 −0.7 ms）；戰鬥追蹤裡複製出的 242 個元素沒有一個和快取共用、快取沒有被寫入、四次擊倒各取到 13 格。程式推送的三方核對記在[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
> 原版（Wine 下的中文版原版，只跑了一場戰鬥）同型第二次擊倒有和第一次同種類的完整演出；移植版修正後逐格擷取，四次擊倒都經過同樣的階段、順序和原版相同（立繪在總傷害出現之前離場），修前同型第二次的立繪站到總傷害出現之後。只比畫面內容與順序。陣列語義的期望多由靜態反組譯推演、沒有在原版上實跑；片頭編號的轉換證明用的是私人工具；一般的 struct 指派、Copy 一族與 `A_REF` 的複本不在範圍內。詳見[struct 值陣列研究](docs/checkpoints/2026-09-28/research/array-struct-copy/README.md)與[審查](docs/checkpoints/2026-09-28/research/array-struct-copy/review.md)。
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
