xsystem4
========

> **最新狀態（2026-10-26）：G11 v14 系統音效表（SE 表），程式 `4159ce6` 已推送（推送後本機、ls-remote 與 GitHub branches API 三方一致），parent `7496f55`（G4 的文件提交；在 `cdff563` 上開發、審查與做提交前驗收，推送前 cherry-pick 到 `7496f55`，`src`／`include` 的增刪內容兩邊相同），libsys4 `247f544`。提交前預設與 GBK 各 120 模式 PASS（多的是新探針 `sound-se`）、提交後各 122 模式 PASS（120 個的結果和提交前相同，多的兩個是新 parent 帶來的 `layer-input`、`parts-sound-name`），sanitizer 0；修前新探針 `sound-se` 對 `cdff563` 兩種字集各 14 案中 9 案失敗（系統音效和腳本的一般通道共用號碼池、StopSe／IsPlaySe 動到同號的通道、GetFreeSeID 不看範圍、SetSeParam 是空殼、重啟沒有表可清等）、資料自檢與四個守衛案通過；最終 parent `7496f55` 的 before-check（預設字集）同樣 9/14 失敗，輸出和提交前逐行相同。133 個突變（作者的與審查者的）中，判不等價的 108 個兩種字集都抓到，判等價的 25 個存活。提交前九路線對 G1b 的提交後基準日誌與操作門檻通過，對白逐位元組相同、點擊位置相同，點擊目標只有 fightr 差 56（落在歷來出現過的分群；G11 會不會改變比例分辨不出）；提交後九路線對 G4 的提交後基準通過，對白逐字相同、點擊位置相同，點擊目標只有 fightr 差 3；提交後同一支 binary 片頭三趟新判準各自通過（S−錨點 +7.6、−1.9、−0.3 ms）、組層級通過（平均 +1.8 ms），舊判準 50/50、50/50、48/50。實作前的探針審查 1 個 High、4 個 Low 都照改；實作後的兩個審查沒有 High、共 4 個 Medium，新代理處理、程式沒動；之後四次唯讀複核各 1 個 Medium（都是探針少一案，補子案與突變、程式沒動），第四次之後依收斂規則收編；推送前一個唯讀覆核（證據重算與公開內容與範圍合併由同一個審查者做，代號 PP-）對程式沒有 High／Medium，判可以推送；文件唯一的 Low（PP-L1）已改。**
> [本次系統音效表](docs/checkpoints/2026-09-28/research/sound-se/README.md) · [元件音效](docs/checkpoints/2026-09-28/research/parts-sound-name/README.md) · [子畫面輸入範圍](docs/checkpoints/2026-09-28/research/layer-input/README.md) · [聲音播完倒帶](docs/checkpoints/2026-09-28/research/sound-rewind/README.md) · [構築預覽](docs/checkpoints/2026-09-28/research/construction-preview/README.md) · [聲音增益](docs/checkpoints/2026-09-28/research/sound-gain/README.md) · [聲音群組](docs/checkpoints/2026-09-28/research/sound-groups/README.md) · [滾輪](docs/checkpoints/2026-09-28/research/wheel-events/README.md) · [按鍵盤點](docs/checkpoints/2026-09-28/research/key-map/README.md) · [結束鍵](docs/checkpoints/2026-09-28/research/end-keys/README.md) · [滑過事件](docs/checkpoints/2026-09-28/research/hover-events/README.md) · [自動輸入](docs/checkpoints/2026-09-28/research/auto-input/README.md) · [CG 翻轉](docs/checkpoints/2026-09-28/research/cg-deform/README.md) · [構築命令](docs/checkpoints/2026-09-28/research/construction-shapes/README.md) · [物品框](docs/checkpoints/2026-09-28/research/cg-scale-copy/README.md) · [成就橫幅](docs/checkpoints/2026-09-28/research/gradation-text/README.md) · [按鈕文字](docs/checkpoints/2026-09-28/research/button-text/README.md) · [X／Y 旋轉](docs/checkpoints/2026-09-28/research/parts-rotate-xy/README.md) · [技能欄](docs/checkpoints/2026-09-28/research/activity-release/README.md) · [捲軸把手](docs/checkpoints/2026-09-28/research/scrollbar-knob/README.md) · [APEG 影像解碼](docs/checkpoints/2026-09-28/research/apeg-video/README.md)。**仍不能當作完整遊戲使用。** v14 的系統音效（游標、點擊、取消、結算與語音等經 `PlaySystemSE` 播的聲音）照原版放進聲音引擎自己的 SE 表，和腳本的一般通道分開；修前兩邊共用號碼池，一邊會切掉另一邊。實機錄音（混音器的輸出）錄到的是「一般 ID 7 的 Prepare 切掉 SE 7 的語音」這個方向：修前語音在數值循環開始時被切掉，修後整段播完、和循環同時都在；簡報設想的另一個方向（循環在播時同號的 PlaySe）實機沒有出現，只由探針的合成資料涵蓋。遊戲設的 40 ms 同名抑制與「語音不可同時播」照原版生效，StopSe、IsPlaySe 只動自己那個 ID，GetFreeSeID、IsExistSeID 照原版的算術（腳本沒有呼叫）；系統音效的群組與內部音量不變（原版也是音效鍵）。只在 v14 的聲音群組模式生效，其他遊戲照舊。移植策略：PlaySpan 用移植版自己的播放位置量（遊戲的 40 ms 實際在 46–70 ms 放行，連按游標時推定比原版多擋一些，沒有實機聽）、重用同名通道時關掉再開（約 23 ms 的空隙）、重啟時清空表（推論原版重建聲音引擎）。引擎自己的聲音（元件音效）原版也走這張表的 SE 3，移植版仍走 G4 的匿名通道，40 ms 同名抑制沒做；G11 推送後可以把元件音效改走 SE 3。第四次複核另列的五個存活突變沒有收（在最終探針的前一版上量的）。G4、G3b、G1b、G7 的待辦照舊；原版複合 owner／內部焦點、既有右鍵隱藏與畫面差異、SJIS 及長時穩定仍有限制。接著依序做 G8、U1 上游小修（多行文字存讀檔的換行、片頭音訊的樣本數、parts 的幾處洩漏、Asin／Acos 回傳弧度；可和 G8 並行）、G9、G10、G12、U2（字串編碼判定收緊＋6 號馬賽克），再做 G1c（靜態緩衝區不套迴圈點，低優先）；上游 7/5–10/25 的提交已逐項評估，個別挑。10/26 起由 Codex 接手，交接在私人報告。詳見[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
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
