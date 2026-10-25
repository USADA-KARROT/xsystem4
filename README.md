xsystem4
========

> **最新狀態（2026-10-25）：v14 構築命令 1／12／13／89 程式 `48d921e` 已推送，三方雜湊一致，parent `97fee1d`（G2 的文件提交），libsys4 `247f544`。提交前預設與 GBK 各 117 模式 PASS、提交後各 118 模式 PASS（多的是 G2 的 `sound-gain`），sanitizer 0；修前探針對 `e69dfa3` 兩種字集各 6/8 案失敗（原因都包含狀態沒有建構，PV7 另有存檔仍是第 12 版）、兩個守衛案通過，最終 parent `97fee1d` 的 before-check（預設字集）同樣 6/8 失敗、守衛通過。提交前／後九路線日誌與操作門檻通過，提交後的對白和 G2 的提交後基準逐字相同；片頭提交前同一支 binary 兩趟新判準都通過，舊判準 49/50（t311，片頭載入只卡 43 ms 的那一趟）與 50/50 照記；提交後同一支 binary 三趟新判準各自通過、組層級通過（平均 +0.0 ms），舊判準 50/50。實作前的探針審查有 2 個 Medium，都已在實作前照改（都更嚴、沒有放寬）；實作後的語義、證據兩個審查都沒有 High／Medium；推送前兩個覆核對程式都沒有 High／Medium（文件定稿裡的一個 Medium 已改）。**
> [本次構築預覽](docs/checkpoints/2026-09-28/research/construction-preview/README.md) · [聲音增益](docs/checkpoints/2026-09-28/research/sound-gain/README.md) · [聲音群組](docs/checkpoints/2026-09-28/research/sound-groups/README.md) · [滾輪](docs/checkpoints/2026-09-28/research/wheel-events/README.md) · [按鍵盤點](docs/checkpoints/2026-09-28/research/key-map/README.md) · [結束鍵](docs/checkpoints/2026-09-28/research/end-keys/README.md) · [滑過事件](docs/checkpoints/2026-09-28/research/hover-events/README.md) · [自動輸入](docs/checkpoints/2026-09-28/research/auto-input/README.md) · [CG 翻轉](docs/checkpoints/2026-09-28/research/cg-deform/README.md) · [構築命令](docs/checkpoints/2026-09-28/research/construction-shapes/README.md) · [物品框](docs/checkpoints/2026-09-28/research/cg-scale-copy/README.md) · [成就橫幅](docs/checkpoints/2026-09-28/research/gradation-text/README.md) · [按鈕文字](docs/checkpoints/2026-09-28/research/button-text/README.md) · [X／Y 旋轉](docs/checkpoints/2026-09-28/research/parts-rotate-xy/README.md) · [技能欄](docs/checkpoints/2026-09-28/research/activity-release/README.md) · [捲軸把手](docs/checkpoints/2026-09-28/research/scrollbar-knob/README.md) · [APEG 影像解碼](docs/checkpoints/2026-09-28/research/apeg-video/README.md)。**仍不能當作完整遊戲使用。** 設定第 3 頁（信息窗口）的預覽背景照原版取背景的下半張縮成 640×180 混合上去；右下三個按鈕（返回遊戲、返回標題畫面、結束遊戲）修前被照原尺寸畫的背景擋住、按中間點不到，修後實測都可用。預覽除了樣本信息窗口蓋到的地方，和模型逐像素相同；原版依據是反組譯加原版 JPEG 截圖的殘差；窗口偏淡、沒有樣本文字屬 G9。命令 12（事件提示的條紋、一種捲軸底）沒有走到畫面、沒有並排；命令 89 的全畫面部件建構後照舊隱藏、不擋點擊，它在原版的用途是推論；命令 1 用在一般進不去的設定第 8、9 頁。存檔升到 XPE 第 13 版，v14 的新存檔不能被舊建置讀。PV6 等「不建構」的條件多是移植策略；缺鍵沒有探針守、12 與 14 對沒有 alpha 的 CG 處理不一致、CG 解析度比例沒有重現，列為後續。英文原版因螢幕鎖定沒補拍。G2 留下的待辦照舊（G1 的回歸：聲音自然播完不倒回開頭；兩處註解與 A-L1／A-L2），系統音效的內部音量鍵沒追（G11）、plmpeg 後端沒有建置。原版複合 owner／內部焦點與候選 scope、既有右鍵隱藏與畫面差異、SJIS 及長時穩定仍有限制。接著依序做 G1b（播完不倒回開頭，讀程式發現；連同 G2 的待辦）、子畫面輸入範圍／多送的 MouseEnter、G4、G8、G9（信息窗口，依賴 G7），再做其餘。詳見[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
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
