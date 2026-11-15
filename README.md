xsystem4
========

> **最新狀態（2026-11-15）：B2 的提交前驗收、提交後驗收與推送前覆核完成。**AIN v14 的 struct 成員宣告為列舉陣列時，物件不跑建構子建出（`init_struct_slot`）就拿到泛型陣列頁，而 `X_A_INIT` 對同一宣告給的是 int 頁；泛型頁的拆除與複製把元素當成堆積槽號，列舉值因此被當成參照扣掉。本組讓頁型別一律由宣告決定：`init_struct_slot` 對列舉陣列成員改給 int 頁；`system.DeserializeStruct` 的目的頁是泛型、宣告的元素是值時，新頁改用宣告的頁型別；讀入 v14 VM 映像之後，struct 成員宣告為值陣列、指到的卻是一維泛型頁時，改成宣告的頁型別；v14 之前不變（讀程式；載入時的新條件只對 v14 才有的泛型頁成立，屬推論）。遊戲裡這造成讀存檔之後下一場戰鬥的擊倒結算畫面點不掉：讀檔還原的戰鬥資料被拆時，陣列裡兩個列舉值剛好是兩個全域物件的槽號，其中一個是把整畫面輸入交給腳本的管理物件；這個缺陷在 11/07 以來的四個版本都重現，不是近期改動的退步，會不會發作取決於存檔裡的值。程式 `ad0d4b9`（父提交 `24919ee`，模式 142 → 143）：新模式 `array-enum-member` 20 案，父提交上 8 案失敗、沒有崩潰；預設與 GBK 各 143 模式通過、sanitizer 0；從讀取畫面讀存檔、新開局兩條路線各兩場戰鬥的結算都能點掉並回到地圖；九條路線與片頭三趟照既有判準通過（片頭判準要對上的元件編號改用 B1 之後的值；本組沒有追蹤趟，屬間接證據）；提交後在程式提交上就地重建重跑，修前對照、兩組 143 模式、九條路線、片頭三趟與讀存檔路線的結算都照預期；提交後的片頭判定，包裝腳本在只作記錄的舊式判準中斷（它的參考檔被誤刪，已重新產生並核對），正式判準直接執行。程式推送的三方核對記在[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
> 讀檔方式的驗證範圍：已實機驗證的是從讀取畫面讀玩家存檔與新開局；舊版本寫的引擎快照由新版本讀回，正常遊玩沒有入口，尚未實機驗證，只在人工情境下量到修補有執行、改對了頁、值保留、兩個全域物件沒被誤刪、那一場的結算能關。**給玩家的注意事項**：戰鬥全滅後，遊戲結束畫面上方的兩個重試鈕（回到一格之前、回到戰鬥之前）不要選；選了並確認，程式會直接結束、沒有提示，上次存檔之後的進度會不見。下方的讀取與回標題正常，請改用讀取。原因是預設格式的引擎快照讀不回（在敵方數值調高的人工條件下量到；新開局路線的教學戰選回到戰鬥之前的重試，父提交與本組都一樣結束（exit 139）、停在同一行，不是本組造成的，本組不改）。原版行為只來自靜態反組譯，沒有在原版上實跑。詳見[列舉陣列成員研究](docs/checkpoints/2026-09-28/research/array-enum-member/README.md)與[審查](docs/checkpoints/2026-09-28/research/array-enum-member/review.md)。
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
