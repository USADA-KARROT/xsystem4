xsystem4
========

> **最新狀態（2026-11-15）：W1 的提交前驗收、提交後驗收與推送前覆核完成。**AIN v14 進入函式時，移植版替每個 wrap 型的區域變數新配一格的頁（以下稱盒子）當初值；v14 的位元組碼對宣告時就給值的變數與 foreach 的元素變數，第一次寫入前沒有 `DELETE`，而 v14 的 `X_ASSIGN` 不釋放被蓋掉的值，所以這種變數每呼叫一次就留下一個沒有持有者的盒子。原版（`0x679970` → `0x656b10` → `0x656970`）對這種變數寫 -1、不配任何物件。本組照原版改兩處：一般函式呼叫與劇本頁（`CALLONJUMP` 依名字預先建的劇本函式區域頁）建區域頁時，v14 的 wrap 型區域變數從 -1 開始（`src/vm.c` 的 `local_initval()`）；v14 之前、全域、struct 成員與陣列元素不變。遊戲裡這些盒子隨遊玩時間累積（地圖上完全不動每秒約 1,100 個，戰鬥中更多），腳本在地圖每走一格、進戰鬥與戰鬥結束時把整個堆積寫成引擎快照，那一下停頓跟著變長（每多 10 萬個活槽約多 18 ms）；只有讀檔或回標題會清掉。程式 `2227b07`（父提交 `5922dc0`，模式 143 → 144）：新模式 `wrap-local-init` 10 案，父提交上 8 案失敗、沒有崩潰；預設與 GBK 各 144 模式通過、sanitizer 0；九條路線與片頭三趟照既有判準通過（片頭判準要對上的元件編號沿用 B2 的值，非預設值、判準工具是私人的；本組沒有追蹤趟，屬間接證據）；提交後在程式提交上就地重建重跑，修前對照、兩組 144 模式、九條路線、片頭三趟、讀存檔路線的結算與連戰對照都照預期；同一情境、同一輸入的修前修後對照：修後 19 個快照裡沒有持有者的盒子都是 0（修前算過的 6 個是 4.7 萬到 97.8 萬），地圖上不動 250 秒活槽只多 48 個（修前 27.3 萬），只寫快照、沒有別的切換的那幾窗，最長一幀都比修前短（例如進第三場 281 → 148 ms）。推送前覆核兩份（互相獨立、唯讀）都判程式提交可以推送、沒有 High，文件的意見已照改（見[審查](docs/checkpoints/2026-09-28/research/wrap-local-init/review.md)）。程式推送的三方核對記在[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
> **給玩家**：這一版起，不讀檔連續遊玩時，地圖走一格、進戰鬥、戰鬥結束的那一下停頓，成長慢了很多：修後連打三場（約 12 分鐘）看不出變長；戰鬥中仍有量小得多的同類累積（每場活槽仍多 1.5 萬到 4.3 萬，修前是 21 萬到 36 萬），更長時間的遊玩會不會再變長沒有量；戰鬥中的幀率修前修後相同。用舊版本時，覺得這幾種切換越來越卡，可以存檔、回標題、再讀檔：讀檔或回標題之後，累積的東西清回起點，這是量到的。停頓跟著變短，是由「停頓隨堆積大小變長」推的。記憶體不會全部回落（堆積陣列不縮）。和這次無關、仍然存在的：戰鬥全滅後，遊戲結束畫面上方的兩個重試鈕（回到一格之前、回到戰鬥之前）不要選；選了並確認，程式會直接結束、沒有提示，上次存檔之後的進度會不見。下方的讀取與回標題正常，請改用讀取。原版行為只來自靜態反組譯，沒有在原版上實跑。詳見[wrap 區域變數初值研究](docs/checkpoints/2026-09-28/research/wrap-local-init/README.md)與[審查](docs/checkpoints/2026-09-28/research/wrap-local-init/review.md)。
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
