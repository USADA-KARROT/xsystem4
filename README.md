xsystem4
========

> **最新狀態（2026-11-16）：R1a 的提交前驗收、提交後驗收與推送前覆核完成。**腳本把整個 VM 寫成引擎快照（`system.ResumeSave`），遊戲結束畫面的兩個重試鈕與回想的重播再讀回來（`system.ResumeLoad`）。AIN v14 用預設格式（rsm）時讀不回、選重試程式就結束，原因是移植版在快照裡的寫法與讀入端沒有對齊的幾個獨立缺口：沒有結構編號的一格 struct 頁（移植版給 v14 的 wrap 值所用）讀入時越界讀、同名的多載函式讀回成同名第一個、rsm 讀入後沒有重建空閒清單而且清空堆積時留下已釋放的指標（之後字串被再釋放一次）、第 0 號槽的保護頁讀回成空頁、讀入不檢查呼叫框數。本組只改 `src/resume.c`、`src/vm.c`、`include/vm.h`，讓移植版寫的快照讀得回來、讀回後的堆積是 VM 預期的狀態：空結構名的頁讀成編號 -1、查不到的名字明確報錯；後面的多載寫成 libsys4 已支援的「名字#n」；兩種格式讀入後重建空閒清單與 delegate 目標表、換回保護頁，清空堆積時清掉型別與指標；框數超過 4096 明確報錯。檔案格式、預設格式、libsys4 與既有的錯誤訊息不變；讀不回時仍然結束程式。有和原版寫的快照對照過：原版是 rsm 第 14 版、另一套配置，移植版寫 libsys4 的第 9 版，互不相容，這一組不追求相容。讀回後呼叫框的幾個內部欄位沒有還原，是讀回之後操作不正常的原因之一，這一組沒有涵蓋到（提交之後才查清楚，下一組修）。程式 `372c83e`（父提交 `1667e11`，模式 144 → 145）：新模式 `resume-image` 33 案，修前 sanitizer 版 19 案失敗、14 案是護欄，修後 33／33，既有探針沒有一案改變；15 個突變中 14 個被擋下；兩輪只改註解的小修正之後與程式提交上重跑，預設與 GBK 各 145 模式通過、sanitizer 0；九條路線與片頭三趟提交前後都照既有判準通過（片頭判準要對上的元件編號沿用 W1 的值，非預設值、判準工具是私人的；本組沒有追蹤趟，屬間接證據；提交後三趟的逐像素比對沒有獨立重算）；實機上兩個重試鈕與回想的重播修前都讓程式結束（退出碼 1／134／134），修後都不結束；所有實機趟都在螢幕鎖定下跑，重試與打贏用的是人工條件，沒有和原版同情境的畫面對照。推送前覆核兩份（互相獨立、唯讀）都判程式提交可以推送，文件的意見已照改（見[審查](docs/checkpoints/2026-09-28/research/resume-image/review.md)）。程式推送的三方核對記在[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
> **給玩家**：戰鬥全滅後，遊戲結束畫面上方的兩個重試鈕都不會再讓遊戲直接關掉。回到戰鬥之前：會回到同一場戰鬥的第一回合、可以重打，用掉一個重試道具；**打贏之後回到地圖時畫面全黑**（音樂還在）。回到一格之前：按了之後畫面全黑、地圖點了沒有反應，目前不要用。遇到全黑時的出口：按 Esc 叫出設定頁（看得見）→ 選回標題 → 確認框按確定 → 從標題讀存檔；上次存檔之後的進度會失去，重試打贏的成果也保不住；右鍵與地圖上的系統鈕沒有用（系統選單開得出來但看不見，沒辦法從那裡存檔）。所以最穩的做法仍然是全滅時直接讀檔；進地城前先存檔。鑑賞模式的回想：前一版看完回想程式會直接結束；這一版看完會回到回想頁，畫面正常，要按兩次返回才回到標題。全黑與操作不正常是讀回之後畫面與操作的還原還沒做，下一組處理。這些是在人工條件下量到的（敵方數值調高的遊戲資料複本），全部在螢幕鎖定下跑，沒有用原版數值輸過，也沒有和原版的畫面對照。詳見[引擎快照讀回研究](docs/checkpoints/2026-09-28/research/resume-image/README.md)與[審查](docs/checkpoints/2026-09-28/research/resume-image/review.md)。
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
