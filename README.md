xsystem4
========

> **最新狀態（2026-10-26）：G4 v14 元件的指針音效、點擊音效與オン指針表示連動，程式 `7baaa12` 已推送（推送後本機、ls-remote 與 GitHub branches API 三方一致），parent `d3908e5`（G3b 的文件提交；直接在上面開發與做提交前驗收，沒有換基底），libsys4 `247f544`。提交前預設與 GBK 各 121 模式 PASS（多的是新探針 `parts-sound-name`）、提交後各 121 模式 PASS（每個模式的結果和提交前相同），sanitizer 0；修前新探針 `parts-sound-name` 對 `d3908e5` 兩種字集各 16 案中 11 案失敗（Set／Get／PlaySound 與連動的 Get 是空殼、pactex 的鍵沒讀、進入與放開都沒播、存檔沒有名稱、遊戲模式的存檔沒有 PSND、框那一層的按鈕不響、引擎的聲音佔走腳本的通道號碼等）、五個守衛案通過；最終 parent `d3908e5` 的 before-check（預設字集）同樣 11/16 失敗、守衛通過，輸出和提交前逐行相同。作者 40 個突變都抓到；審查者另寫的突變存活 6 個（1 個等價）。提交前九路線對 G3b 的提交後基準日誌與操作門檻通過，對白逐位元組相同、點擊位置相同，點擊目標只有 fightr 差 63（既有分群例外）；片頭提交前一趟新判準通過、舊判準 50/50；提交後九路線對同一個基準日誌與操作門檻通過、對白逐字相同、點擊位置相同，點擊目標只有 fightr 差 55（既有分群例外）；提交後同一支 binary 片頭三趟新判準各自通過（S−錨點 −2.0、+2.7、−5.0 ms）、組層級通過（平均 −1.4 ms、標準差 3.9 ms），舊判準三趟 50/50。實作前的探針審查沒有 High（3 個 Medium、4 個 Low 都照改）；實作後的兩個審查沒有 High、各 1 個 Medium（遊戲模式的存檔沒有探針守、錄音數字沒有證據檔），新代理補探針與證據檔、程式沒動，唯讀複審通過；推送前兩個唯讀覆核（證據重算、公開內容與範圍）對程式都沒有 High／Medium；文件的 Low（第三趟片頭桌面游標的寫法、匿名通道的列舉漏了 DALKDemo、英文版 SJIS 那一句範圍太寬等）已改。**
> [本次元件音效](docs/checkpoints/2026-09-28/research/parts-sound-name/README.md) · [子畫面輸入範圍](docs/checkpoints/2026-09-28/research/layer-input/README.md) · [聲音播完倒帶](docs/checkpoints/2026-09-28/research/sound-rewind/README.md) · [構築預覽](docs/checkpoints/2026-09-28/research/construction-preview/README.md) · [聲音增益](docs/checkpoints/2026-09-28/research/sound-gain/README.md) · [聲音群組](docs/checkpoints/2026-09-28/research/sound-groups/README.md) · [滾輪](docs/checkpoints/2026-09-28/research/wheel-events/README.md) · [按鍵盤點](docs/checkpoints/2026-09-28/research/key-map/README.md) · [結束鍵](docs/checkpoints/2026-09-28/research/end-keys/README.md) · [滑過事件](docs/checkpoints/2026-09-28/research/hover-events/README.md) · [自動輸入](docs/checkpoints/2026-09-28/research/auto-input/README.md) · [CG 翻轉](docs/checkpoints/2026-09-28/research/cg-deform/README.md) · [構築命令](docs/checkpoints/2026-09-28/research/construction-shapes/README.md) · [物品框](docs/checkpoints/2026-09-28/research/cg-scale-copy/README.md) · [成就橫幅](docs/checkpoints/2026-09-28/research/gradation-text/README.md) · [按鈕文字](docs/checkpoints/2026-09-28/research/button-text/README.md) · [X／Y 旋轉](docs/checkpoints/2026-09-28/research/parts-rotate-xy/README.md) · [技能欄](docs/checkpoints/2026-09-28/research/activity-release/README.md) · [捲軸把手](docs/checkpoints/2026-09-28/research/scrollbar-knob/README.md) · [APEG 影像解碼](docs/checkpoints/2026-09-28/research/apeg-video/README.md)。**仍不能當作完整遊戲使用。** v14 元件的滑過音與點擊音照原版接上：名稱來自 pactex 的「オン指針音效」「點擊音效」與腳本的 `Parts_SetSoundName`，每次新進入播指針音、點到時播點擊音，停用的按鈕與子畫面底下的按鈕不響。實機錄音（混音器的輸出）裡，按鈕、檢查框與橫捲軸的滑過、按鈕與一個檢查框的點擊，都和音效檔乘上音量設定的增益後相同，音效拉到 0 時無聲；修前開場之後全程無聲。設定第 6 頁（「窗口」頁）的說明框修前四段疊在一起，修後游標不在目標上時是空的、滑過哪一個只顯示那一段，和中文版原版（游標不在目標上的那一張）與英文版原版的截圖一致（英文版 10/26 補拍了最後的「再移開」，說明框清空）。引擎自己的聲音改用自己的通道，不再借 wav 池的空號（修前會和腳本的固定號碼撞號，讀程式加探針量到；不限 v14）。名稱存成選用的 PSND 區塊，存檔版本仍是 XPE 13，新舊存檔兩個方向互讀都實測過。移植策略：名稱在設定時就換成音效封存的號碼（原版以名稱經 PlaySe 3 播）；原版 SE 3 推定的 40 ms 同名抑制沒做（快速劃過相鄰的同名按鈕時移植版每次都響），G11 推送後可改走 SE 3；連動是瞬間顯示或隱藏、只隱藏元件本身（原版約 100 ms 淡入淡出），被隱藏的元件仍被游標走訪進入；SJIS 的 pactex 鍵沒讀。「Failed to load WAV -1」在 G3b 的基準已經是 0，不是修好的證據。審查者另寫而存活的突變代表的探針缺口（停用按鈕的 `Parts_PlaySound`、連到根元件的正式名稱、對元件 0 Set 連動、隱藏元件的名稱）列為後續。G3b 排給下一個碰 `input.c` 的兩句註解與 G7 的兩處註解待辦這次沒做，照舊；G1b、G3b 的其他待辦與 plmpeg 後端沒有建置照舊；原版複合 owner／內部焦點、既有右鍵隱藏與畫面差異、SJIS 及長時穩定仍有限制。10/26 也在英文版原版補拍了 G3b 留下的兩張：存檔確認框與讀取確認框開著時，游標移到框內空白或框外，框下的存檔鈕與那一列都保持滑過的顏色，和 G3b 修後一致，原版是「只走作用中層」。接著依序做 G11（系統音效表，收尾中）、G8、G9、G10、G12，再做 G1c（靜態緩衝區不套迴圈點，低優先）；另列 U1 上游小修（多行文字存讀檔的換行、片頭音訊的樣本數、parts 的幾處洩漏、Asin／Acos 回傳弧度；可和 G8 並行）與 U2（字串編碼判定收緊＋6 號馬賽克）；上游 7/5–10/25 的提交已逐項評估，個別挑。詳見[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
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
