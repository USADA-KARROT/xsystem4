xsystem4
========

> **最新狀態（2026-11-23）：R1c 提交一（引擎快照讀不回時告訴玩家並結束）的提交前驗收與提交後驗收都沒有不通過的項目；真的訊息框在提交後用正式建置跑了三趟，自動截圖看得到框與檔名、由程式按下確定、之後以結束碼 1 結束，沒有人在現場用眼睛看或用滑鼠點過；推送前覆核完成，兩位覆核者都判程式提交可以推送，對文件的意見已在這次文件改正。**腳本把整個 VM 寫成引擎快照（`system.ResumeSave`），遊戲結束畫面的兩個重試鈕與回想的重播再讀回來（`system.ResumeLoad`）。AIN v14 的快照讀不回時，移植版以前在紀錄印一行錯誤就結束、沒有告訴玩家，有些壞資料讓程式當掉或卡住，檔案不存在時跳過讀回、腳本接著跑（重試後黑畫面卡住），存檔資料夾裡有原版格式的續玩檔時每次開機都結束；rsm 寫入失敗仍算存好、留下截斷的檔（json 回報失敗，但舊檔已被截斷）。原版（這個移植跑的那一版的執行檔，靜態反組譯，從未執行）在讀不回時開一個寫出檔名的訊息框、停止遊戲（`0x672960` → `0x668d30`；VM 的某個欄位是 2 時不開框、不停），函式檢查碼不符時改走 `system.Error` 的框；`ResumeSave`（`0x6727e0`）一律推 1、成敗寫進第三個引數。本組改引擎 4 個檔（`src/resume.c`、`src/vm.c`、`src/hll/system.c`、`include/vm.h`）：照原版——讀不回（含檔案不存在）就告訴玩家並結束、不再先查檔案、`ResumeSave` 回傳真；移植版自己的做法——一個按鈕的框與自己的文字、框不看 VM 狀態、檢查碼不符也結束，libsys4 讀不了的版本（原版寫的第 14 版）不讀、腳本接著跑，先寫暫存檔再改名，讀入時的各項檢查與堆積格數上限（不讓壞檔當掉、卡住或越界讀），libsys4 不改。程式 `3d31ef1`（父提交 `61985f2`，模式 158 → 159）：新模式 `resume-unreadable` 71 案，父提交的引擎上 62 案失敗、修後 71／71；19 個既有模式相同；62 份較早的快照離線讀回沒有一份被新規則誤擋；突變 110 列沒有和預測相反的；兩組各 159 模式通過、sanitizer 0。提交前實機（私人來源副本上建的另一支執行檔，和提交的程式碼等價、只差版本字串與路徑字串）：九條路線與片頭三趟照既有判準通過；五種強迫失敗情形（快照不見、被截斷、檢查碼被改、開機時有原版格式的續玩檔、存檔資料夾不可寫）修前修後共 18 趟，修後在自動回答模式下都通過。提交後在程式提交上重建：修前對照、兩組各 159 模式、九條路線、片頭三趟、強迫失敗 7 趟照預期；真的訊息框 3 趟（螢幕沒鎖）看得到框與檔名、按下確定後以結束碼 1 結束；重試與回想的九個專項（提交之後才跑）都通過，升級情境那一趟沒有讀到引擎快照。子模組內部的解析錯誤、框頁檢查擋不住的形狀、舊建置讀新快照不支援、寫入改名前沒有同步、只有資料夾唯讀時快照改成寫不進去（修正排在下一個提交）、腳本的存檔失敗提示玩家看不到等限制照實列在研究；部件影像那一側是下一個提交。推送前覆核：兩位互相獨立的覆核者都判程式提交可以推送（證據覆核 High 0、Medium 0、Low 2（都在文件）、Info 6；公開內容覆核 High 0、Medium 0、Low 2（都在文件）、Info 4；兩位都沒有對程式提交提出 High、Medium 或 Low）；文件的 4 個 Low 都在這次文件改正。程式推送的三方核對記在[交接](docs/checkpoints/2026-09-28/HANDOFF.md)。
> **給玩家**：重試或回想用的資料（引擎快照）讀不回來時（檔案不見了、被截斷或內容壞了），遊戲現在會跳出訊息框，寫出是哪一個檔讀不到、說明遊戲要關閉，按確定後遊戲就關閉，不再黑畫面卡住或沒說一聲就關掉；壞到某些程度的檔仍可能讓遊戲直接關掉或當掉。那一次不能接著玩（照對原版程式的靜態讀法，原版大多數情形也是停止遊戲），重要的進度請另外存檔。存檔資料夾裡有原版留下的續玩檔時，現在可以正常開機到標題（那個檔不會被讀，也不會被刪）。這個訊息框由自動測試的截圖確認過（框與框裡的檔名都看得到），也由測試程式按下確定、確認遊戲接著關閉；沒有人在現場用眼睛看過或用滑鼠點過。已知差異：只有存檔資料夾本身設成唯讀、裡面的檔可以寫時，以前快照照樣寫得進去，現在寫不進去（照程式的設計，之後按重試會回到更早存好的那一份，一份都沒有時跳出這個框並關閉，這兩件沒有實機試過；遊戲目前不會為存檔失敗跳提示；修正排在下一個提交）。看到這個框，請回報框裡的檔名與當時在做什麼。詳見[快照讀不回時的研究](docs/checkpoints/2026-09-28/research/resume-unreadable/README.md)與[審查](docs/checkpoints/2026-09-28/research/resume-unreadable/review.md)。
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
