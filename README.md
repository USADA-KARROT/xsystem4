xsystem4
========

> **最新狀態（2026-10-25）：G1b v14 群組通道自然播完後倒回開頭，程式 `3af566d` 已推送（推送後本機、ls-remote 與 GitHub branches API 三方一致），parent `d7c0d27`（G7 的文件提交；在 G2 程式 `ebda614` 上開發與做提交前驗收，提交成 `abb287c` 後 cherry-pick 過來，`src`／`include` 的 diff 不變），libsys4 `247f544`。提交前預設與 GBK 各 118 模式 PASS、提交後各 119 模式 PASS（多的是 G7 的 `construction-preview`），sanitizer 0；修前新探針 `sound-rewind` 對 G2 程式兩種字集各 9 案中 4 案失敗（播完後 GetPos＝檔長、不重新 Prepare 再 Play 沒有聲音、播完後 Stop 不倒帶）、守衛案通過，`sound-gain` 只有 GN5（影片 SoundID 依 ID 釋放）失敗；最終 parent `d7c0d27` 的 before-check（預設字集）同樣 `sound-rewind` 4/9、`sound-gain` 1/9（GN5）失敗、守衛通過。作者 18 個突變都抓到。提交前兩支建置的九路線日誌與操作門檻通過，對白和 G2 的提交後基準逐位元組相同；片頭新判準四趟都通過、同一支 binary 三趟的組層級通過（平均 −3.5 ms）；提交後九路線對 G7 的提交後基準日誌與操作門檻通過、對白逐字相同、點擊位置相同，點擊目標只有 fightr 差 44（既有分群例外）；提交後同一支 binary 三趟新判準各自通過、組層級通過（平均 +2.8 ms、標準差 4.6 ms），舊判準（只跑第一趟）50/50。實作前的探針審查有 2 個 Medium，都已在實作前照改（都更嚴、沒有放寬）；實作後的語義、證據兩個審查都沒有 High／Medium；推送前兩個覆核對程式都沒有 High／Medium（文件定稿裡的一個 Medium 已改）。**
> [本次聲音播完倒帶](docs/checkpoints/2026-09-28/research/sound-rewind/README.md) · [構築預覽](docs/checkpoints/2026-09-28/research/construction-preview/README.md) · [聲音增益](docs/checkpoints/2026-09-28/research/sound-gain/README.md) · [聲音群組](docs/checkpoints/2026-09-28/research/sound-groups/README.md) · [滾輪](docs/checkpoints/2026-09-28/research/wheel-events/README.md) · [按鍵盤點](docs/checkpoints/2026-09-28/research/key-map/README.md) · [結束鍵](docs/checkpoints/2026-09-28/research/end-keys/README.md) · [滑過事件](docs/checkpoints/2026-09-28/research/hover-events/README.md) · [自動輸入](docs/checkpoints/2026-09-28/research/auto-input/README.md) · [CG 翻轉](docs/checkpoints/2026-09-28/research/cg-deform/README.md) · [構築命令](docs/checkpoints/2026-09-28/research/construction-shapes/README.md) · [物品框](docs/checkpoints/2026-09-28/research/cg-scale-copy/README.md) · [成就橫幅](docs/checkpoints/2026-09-28/research/gradation-text/README.md) · [按鈕文字](docs/checkpoints/2026-09-28/research/button-text/README.md) · [X／Y 旋轉](docs/checkpoints/2026-09-28/research/parts-rotate-xy/README.md) · [技能欄](docs/checkpoints/2026-09-28/research/activity-release/README.md) · [捲軸把手](docs/checkpoints/2026-09-28/research/scrollbar-knob/README.md) · [APEG 影像解碼](docs/checkpoints/2026-09-28/research/apeg-video/README.md)。**仍不能當作完整遊戲使用。** 聲音群組模式（多娜多娜）下，一次性的聲音自然播完時倒回 frame 0：之後 IsPlay 假、GetPos 0，不重新 Prepare 就 Play 會從頭播，和原版的靜態緩衝區（音效、語音、系統音）相同；腳本每次 Play 前都先 Prepare，所以實機只有 Prepare 找不到名稱時（舊通道留著、腳本照樣 Play）聽得出差別，路線只證明沒有變差。串流通道（腳本裡只有 BGM）也倒回 0、再 Play 的圈數和原版不同，都是移植策略；原版串流緩衝區播完不倒帶是推論。影片的 SoundID 照原版依 ID 釋放，但只改了 GetGroupNum 那一半，同 ID 的兩部影片仍各有串流；plmpeg 後端的無聲影片和原版相反（只記錄，plmpeg 沒有建置）。舊路照舊截掉一次性聲音的最後一塊，新探針精確守著這個指紋。新發現：原版的靜態緩衝區不存迴圈點，音效與語音只會整檔循環，G1 的群組模式卻對所有通道套 timemap 的迴圈點，受影響的列數還沒量。G7 排給這個程式提交的兩處（命令 1 的註解、89 的位址寫法）沒做，移到下一個程式提交；系統音效的內部音量鍵沒追（G11）、plmpeg 後端沒有建置照舊。原版複合 owner／內部焦點與候選 scope、既有右鍵隱藏與畫面差異、SJIS 及長時穩定仍有限制。接著依序做 G3b（子畫面輸入範圍／多送的 MouseEnter，進行中）、G4、G8、G9、G10、G11、G12，再做其餘。詳見[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
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
