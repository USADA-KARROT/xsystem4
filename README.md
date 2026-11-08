xsystem4
========

> **最新狀態（2026-11-09）：G14 修正 v14 的 HLL 呼叫歸還交出的參照：CALLHLL 的值型 `hll_param`，型別參數低 16 位是 2、沒有 wrap 位元時，值帶著自己的一份參照交給呼叫，呼叫後歸還。春銷派出的人物卡與場景結束時其餘的卡因此解構、activity 釋放，轉盤最前面不再留著已派出的卡（名牌前面多一個字、適性標記底下疊著舊標記）。程式提交 `d142176`，父提交 `0d41a04`，libsys4 固定 `247f544`。只在 v14、只對頁（struct、陣列、delegate）；字串照這個 VM 的暫存旗標，v13 以前不變，沒有 wrap 位元的介面不處理；不分字集。新模式 `hll-param-handover` 8 案（136 → 137 模式）；修前兩種字集恰好 HP1–HP4 失敗。突變 24 個全被抓到、2 個判等價。提交前（在 `8f9cd6d` 上，136 模式）與提交後（137 模式）預設與 GBK 都通過、sanitizer 0；九條路線提交前對 G10a、提交後對 G13 的基準對白相同、錯誤 0、桌面游標不變；片頭三趟新判準各 50/50、組層級通過（提交前平均 −1.0 毫秒、提交後 −2.4 毫秒）。實機第 8、9 天的參照追蹤，修後 10 張卡全部解構、修前 0 張；ASan＋UBSan 建置走過春銷的配對、派出、結果與下一天，沒有記憶體錯誤。原版這個畫面沒有拍，原版的期望只來自反組譯。**
> [HLL 呼叫交出的參照研究](docs/checkpoints/2026-09-28/research/hll-param-handover/README.md) · [前一組訊息框連動隱藏](docs/checkpoints/2026-09-28/research/message-window-showlink/README.md)。原版依據是靜態反組譯（CALLHLL `0x66d470` 取參數時只在型別參數有 wrap 位元而且是物件時 retain，推入結果之後解構參數的 vector、對每個物件值 release 一次，`0x659d46`）。和原版仍不同的有：字串照暫存旗標（原版的呼叫也歸還字串、陣列存自己的複本）、`A_REF` 對一部分 struct 共享頁（既有差異）、沒有 wrap 位元的介面不處理、歸還在推入呼叫結果之前（原版在之後）。卡片以外，顧客總覽關閉時它的顧客卡也跟著釋放（量測；是同一個機制是推論），之後的 activity 實例編號因此改變。仍不能當作完整遊戲使用。下一步：G15（根據地的構築命令 103、107、121，已在本機提交，等這一組推送後接上）、U2（驗收中）；之後依序 G10b → G12a → 每層取樣與取消旗標 → G1c → G12b → 從第 8 天以後試玩。
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
