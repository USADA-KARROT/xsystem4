xsystem4
========

> **最新狀態（2026-11-22）：R1b 的提交前驗收、提交後驗收與推送前覆核完成（重試後連續打贏兩場的那一趟，後段在提交後的執行檔上沒有走到，照序列關閉結算與開關系統選單的證據來自提交前那一支）。**腳本把整個 VM 寫成引擎快照（`system.ResumeSave`），遊戲結束畫面的兩個重試鈕與回想的重播再讀回來（`system.ResumeLoad`）。R1a 讓 AIN v14 的快照讀得回來，但讀回之後畫面與腳本對不上（打贏後回地圖全黑、回到一格之前全黑、回想要按兩次返回）。原因有兩個：腳本存部件用的那個陣列讀回來是空的（`PartsEngine.Save`／`Load` 宣告成 `wrap<array<int>>`、拿到的是呼叫者陣列的槽號，移植版當成 wrap 的盒子），部件引擎讀回時放棄；呼叫框讀回時只有檔案裡的四個欄位，移植版自己加的欄位與委派的游標都是 0，第一次返回就截掉運算堆疊。本組改引擎 8 個檔：`Save` 寫進、`Load` 讀呼叫者的陣列（照原版 `0x5405a0`）；部件影像多帶幾個可選段（活動登錄表與快進停用的部件——這兩樣原版的存出與讀回都有，`0x563b10`／`0x563c20`、`0x551771`／`0x551ee9`——以及使用者元件資料、唯一編號、v14 數字欄位）；v14 讀回的框由返回位址前的呼叫指令補回方法與委派旗標，閉包環境、委派參照與游標寫進快照（rsm 是一則保留的 comment、json 是附加欄位）；另有兩個不分版本的小修正與兩個診斷用環境變數。這些可選段與 comment 都是移植版自己的格式，和原版引擎寫的檔不相容，這一組不追求相容；讀不回時仍然結束程式（下一組）。程式 `7444591`（父提交 `8afc86b`，模式 145 → 157）：新模式 12 個；父提交上九個新模式都有失敗、三個用到這次才加的內部介面而建不起來（沒有修前結果），快進停用段父提交上 11 案中 10 案失敗（在上一個候選建置——補上快進停用段之前的那一支——的引擎加上這次探針的基底上 9 案）、修後 11／11，19 個突變中 16 個被擋下、3 個等價；提交前與程式提交上預設與 GBK 各 157 模式通過、sanitizer 0；九條路線與片頭三趟提交前後都照既有判準通過（片頭判準要對上的元件編號沿用前幾組的值，非預設值、判準工具是私人的；本組沒有追蹤趟，屬間接證據）；重試與回想的九個情境提交前各一趟都有效、堆疊下溢 0，提交後重跑和提交前逐趟相同，只有連續兩勝那一趟在提交後兩次都在第二場第一回合就打贏、自動操作在預期畫面沒出現時停下（之後回到完整的地圖、卡片數字有更新，屬非正式觀察）；全部在螢幕鎖定下跑，重試用的是人工條件，上一個候選建置曾和英文原版在不同進度上比過相同步驟，沒有原版同情境的畫面對照。推送前兩位互相獨立的唯讀覆核者都判程式提交可以推送（證據覆核 High 0、Medium 2、Low 2，都在文件；公開內容覆核對程式提交 High 0、Medium 0、Low 2，對文件 Medium 1、Low 4）；文件的 3 個 Medium 與 6 個 Low 已在這次文件改正，程式註解的 2 個 Low 留到下一組整理。程式推送的三方核對記在[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
> **給玩家**：戰鬥全滅後，遊戲結束畫面上方的兩個重試鈕現在都能用。選「回到戰鬥之前」：回到同一場戰鬥的第一回合重打；打贏之後結算照常，回到地圖時地圖完整（隊員卡和可以點的格子都在），可以接著走、打下一場，下一場打完地圖上隊員卡的數字會更新。選「回到一格之前」：先回到進這場戰鬥之前的地圖，地圖完整，再點同一格就重新進這場戰鬥、從第一回合開始；打贏之後回到地圖，隊員卡的數字會更新，系統選單也開得出來、關得掉。連續兩次全滅時，第二次也能重試（第一次全滅時持有 2 個重試道具，第二次剩 1 個）；道具用完之後的情形沒有測。重試之後撤退，結算畫面是乾淨的。重試、撤退回到據點之後存檔，不關遊戲直接讀回，回到同一天、同樣金額的據點；關掉遊戲再開、再讀這份新存檔還沒有驗過，重要的進度請另外多存一格。之前的版本存的一般存檔照常讀得進來（試了兩份存檔的複本，都能進據點、地城、開戰）。鑑賞模式的回想：看完一段會回到回想頁，按一次返回就回到標題（是按住快轉看完的，沒有測不快轉、自然播完）；再進回想頁，縮圖還在，可以再看。已知的事：打贏之後地圖上的四張隊員卡，顯示的四位可能和重試當下不同（隊伍有五人、卡片只有四張；判斷是戰鬥中換人上場，沒有和原版對照）；重試或回想之後，劇情回顧（看過的對白紀錄）可能不完整，這一點沒有量；快照讀不回時程式仍會直接結束（下一組處理），所以進地城前先存檔仍然是好習慣。這些是在人工條件下量到的（重試的情況用敵方數值調過的遊戲資料複本），全部在螢幕鎖定下跑，每種情況只跑了一兩趟；上一個候選建置曾和英文原版在不同進度上走過相同的步驟，沒有和原版同情境的畫面對照。詳見[讀回後場景狀態研究](docs/checkpoints/2026-09-28/research/resume-scene-state/README.md)與[審查](docs/checkpoints/2026-09-28/research/resume-scene-state/review.md)。
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
