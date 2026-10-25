xsystem4
========

> **最新狀態（2026-10-25）：G3b v14 子畫面開著時只走作用中的那一層，程式 `b77f3b8` 已推送（推送後本機、ls-remote 與 GitHub branches API 三方一致），parent `cdff563`（G1b 的文件提交；在 G7 的文件提交 `d7c0d27` 上開發與做提交前驗收，提交成 `e513294` 後 cherry-pick 過來，`src`／`include` 的 diff 不變），libsys4 `247f544`。提交前預設與 GBK 各 119 模式 PASS、提交後各 120 模式 PASS（多的是 G1b 的 `sound-rewind`），sanitizer 0；修前新探針 `layer-input` 對 `d7c0d27` 兩種字集各 12 案中 9 案失敗（框下的鈕開框就被離開、關框多送 MouseEnter、別層的鈕被走訪、按下與滾輪不限層、沒有指定作用中層時下層也被走訪等）、三個守衛案通過，`controller-id` 只有更正後的 CI12 失敗；最終 parent `cdff563` 的 before-check（預設字集）同樣 9/12 與 CI12 失敗、守衛通過。作者 22 個突變都抓到。提交前九路線日誌與操作門檻通過，對白和 G7 的提交後基準逐位元組相同、點擊位置相同；提交後九路線對 G1b 的提交後基準日誌與操作門檻通過、對白逐字相同、點擊位置相同，點擊目標只有 fightr 差 4（既有分群例外）；片頭提交前一趟新判準通過、舊判準 50/50；提交後同一支 binary 三趟新判準各自通過、組層級通過（平均 +0.4 ms、標準差 11.2 ms），舊判準（只跑第一趟）50/50。實作前的探針審查沒有 High／Medium；實作後的語義審查有 1 個 Medium（公開文件在提交前沒有完成，已由新代理補上、唯讀複審確認），證據審查沒有 High／Medium；推送前兩個覆核對程式都沒有 High（證據重算的 Medium 是提交說明一句和量測相反，不 amend、以研究為準；公開內容覆核的 Medium 在文件定稿，已改）。**
> [本次子畫面輸入範圍](docs/checkpoints/2026-09-28/research/layer-input/README.md) · [聲音播完倒帶](docs/checkpoints/2026-09-28/research/sound-rewind/README.md) · [構築預覽](docs/checkpoints/2026-09-28/research/construction-preview/README.md) · [聲音增益](docs/checkpoints/2026-09-28/research/sound-gain/README.md) · [聲音群組](docs/checkpoints/2026-09-28/research/sound-groups/README.md) · [滾輪](docs/checkpoints/2026-09-28/research/wheel-events/README.md) · [按鍵盤點](docs/checkpoints/2026-09-28/research/key-map/README.md) · [結束鍵](docs/checkpoints/2026-09-28/research/end-keys/README.md) · [滑過事件](docs/checkpoints/2026-09-28/research/hover-events/README.md) · [自動輸入](docs/checkpoints/2026-09-28/research/auto-input/README.md) · [CG 翻轉](docs/checkpoints/2026-09-28/research/cg-deform/README.md) · [構築命令](docs/checkpoints/2026-09-28/research/construction-shapes/README.md) · [物品框](docs/checkpoints/2026-09-28/research/cg-scale-copy/README.md) · [成就橫幅](docs/checkpoints/2026-09-28/research/gradation-text/README.md) · [按鈕文字](docs/checkpoints/2026-09-28/research/button-text/README.md) · [X／Y 旋轉](docs/checkpoints/2026-09-28/research/parts-rotate-xy/README.md) · [技能欄](docs/checkpoints/2026-09-28/research/activity-release/README.md) · [捲軸把手](docs/checkpoints/2026-09-28/research/scrollbar-knob/README.md) · [APEG 影像解碼](docs/checkpoints/2026-09-28/research/apeg-video/README.md)。**仍不能當作完整遊戲使用。** v14 的多層模式下，輸入更新只走作用中的那一層（原版 `0x53e690`），別層的元件不進不出、時間停住：存檔確認框開著時底下的存檔鈕保持滑過（暗黃，和英文版原版相同），關框時游標還在鈕上不再多送 MouseEnter；讀取確認框也和中文版原版一致。英文版原版存檔框「框開著、游標移離存檔鈕」那一張還沒拍；能分辨「只走作用中層」和「背板不擋游標」的，目前是反組譯加中文版原版系統選單同形的一組截圖（前提是原版的系統選單也另開一層，推論）。順帶修回 G3 帶進來、當時沒發現的回歸：玩家技能演出時其餘角色的粉紅剪影（截圖量到，機制是推論）。`controller-id` CI12 的期望值照原版更正。提交說明把存檔確認框下的鈕寫成「仍被滑過、點得到」，實測是開框就被離開，以研究為準；`src/parts/input.c` 的兩句註解（開框動畫期間的 LostFocus 時機、`0x53c830` 的順序）也以研究為準，排到下一個碰它的程式提交。移植版只有一套輸入狀態（原版每層一套），範圍內換層時的細節、`SetEnableInputProcess`、手勢沒做；探針少「換層後第一次更新就按下」一案、`S2 input-scope` 追蹤行可能讓片頭判準無法判定、讀檔後的點擊沒實機走過，都列為後續。G7 的兩處註解待辦、G1b 的待辦、plmpeg 後端沒有建置照舊；原版複合 owner／內部焦點、既有右鍵隱藏與畫面差異、SJIS 及長時穩定仍有限制。接著依序做 G11（系統音效通道池，進行中）、G4（G3b 推送後可開）、G8、G9、G10、G12，再做其餘；另列 SetLoop 後續（原版靜態緩衝區不存迴圈點，受影響的列數待量）。詳見[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
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
