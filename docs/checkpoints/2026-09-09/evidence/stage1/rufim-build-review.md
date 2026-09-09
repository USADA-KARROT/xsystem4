# Rufim 隔離建置與執行前審查

日期：2026-09-09。範圍：重建比較用版本、記錄工具鏈與必要建置修正；沒有啟動遊戲、執行 binary、操作 UI、安裝套件、合併分支或更動使用者原專案。

## 結果

**建置成功，產物為 Mach-O arm64 debug executable。** 純原始碼在 `-Ddebugger=disabled` 下因缺少 vm_stack_trace 宣告而編譯失敗；補入一行標頭引用後成功。這是「原始版本加最小建置修正」，不可描述為完全未修改版本已成功運行。

- Source：`work/stage1/rufim-source`，detached HEAD `589cf2c7599761e30fc7b9a48ef6d8e106ef76df`。
- 配套 submodule：`Rufim/libsys4@703493c702cff32750a5f48f82b6c19e018d43b1`，初始化成功，未修改。
- Binary：`work/stage1/rufim-build/src/xsystem4`。
- Binary SHA-256：`38777ba479515eada360fbae5751591d5794f6a1b3e2c85899c8209de16ff367`。
- 唯一 source diff：`src/util.c` 添加 `#include "vm.h"`；patch 保存在 `work/stage1/rufim-build-compat.patch`。
- `git diff --check` 通過；最終 `git status --porcelain` 只有 ` M src/util.c`。
- 已檢查 workspace ancestors、Rufim clone 及 submodule，未找到適用的 AGENTS.md。

## 工具鏈及編譯旗標

| 項目 | 實際值 |
|---|---|
| OS／架構 | macOS 26.6.2 (25G83)，arm64 |
| C／C++ compiler | Apple clang 21.0.0 (clang-2100.1.1.101)，CommandLineTools clang／clang++ |
| Linker | Apple ld64 1267 |
| Meson／Ninja | 1.11.1／1.13.2 |
| pkg-config | 2.5.1 |
| Bison | Homebrew 3.8.2，`/opt/homebrew/opt/bison/bin/bison` |
| Flex | Homebrew 2.6.4，`/opt/homebrew/opt/flex/bin/flex`（由 native file 固定） |
| Build type | debug，`-O0 -g`，C11／C++17 |
| Meson options | `-Ddebugger=disabled -Dopengles=disabled --wrap-mode=nofallback` |
| libffi 搜尋路徑 | `PKG_CONFIG_PATH=/opt/homebrew/opt/libffi/lib/pkgconfig`，且 Meson 顯式 `-Dpkg_config_path=/opt/homebrew/opt/libffi/lib/pkgconfig` |
| 平行度 | `ninja -j4` |

`rufim-native.ini` 固定 clang、clang++、Bison、Flex、pkg-config 的絕對路徑。Meson host CPU family = aarch64；沒有 Rosetta／x86_64 架構。

依賴版本（pkg-config 報告值）：SDL2 2.32.10、libffi 3.5.2、freetype2 26.6.20、libturbojpeg 3.1.4.1、libwebp 1.6.0、libpng 1.6.58、cglm 0.9.6、sndfile 1.2.2、libavcodec 62.28.101、libavformat 62.12.101、libavutil 60.26.101、libswscale 9.5.101、glew 2.3.1、zlib 1.2.12。FreeType 這裡列的是 `.pc` 版本欄位，不把它轉稱套件發布版本。

OpenGL 使用系統 framework；libsys4 的 CoreFoundation framework 正常。沒有 libdeflate，使用既有 zlib fallback；chibi-scheme/readline 未偵測到，但 debugger 已 disabled，非必要依賴。沒有下載或安裝新套件。

`otool -L` 實際確認連結 `/opt/homebrew/opt/libffi/lib/libffi.8.dylib`、Homebrew SDL2／FFmpeg／GLEW 及 macOS OpenGL framework，並非系統 libffi。完整連結清單在 `rufim-build-verification.log`。

## 建置嘗試與最小修正

1. 原始 configure 成功，但使用 `/usr/bin/bison` 2.3；它無法解析 libsys4 `src/ini_parser.y:1` 的 `%define api.prefix {yini_}`。這是工具版本問題，未修改 parser 原始碼。
2. 初次 configure 的 Meson `pkg_config_path=[]` 被保存；僅用環境變數和 `--reconfigure --clearcache` 仍辨識 system libffi 3.4-rc1。因此改用 native file、重新 `--wipe` 隔離 build 目錄，並顯式設定 `-Dpkg_config_path=...`；之後輸出確認 libffi 3.5.2。
3. 正確工具鏈下，純原始碼在 `src/util.c:203` 呼叫 `vm_stack_trace()` 時失敗。`debugger.h` 的間接 vm.h include 在 debugger disabled 後消失；函式本身實作在 vm.c，宣告在 vm.h。只在 util.c 直接 include vm.h 即可，沒有變更遊戲／VM 行為。
4. 再執行 ninja 成功連結。仍有原分支編譯 warnings（例如 unused、前置型別宣告、RAND_MAX float 轉換、duplicate `-lz`、section alignment）；本階段沒有為了清 warning 擴大修改。

重建指令應從本任務 workspace 執行（首次 setup 不加 `--wipe`；重建現有目錄可用 `--reconfigure` 並保留 native file）：

```sh
PKG_CONFIG_PATH=/opt/homebrew/opt/libffi/lib/pkgconfig meson setup \
  work/stage1/rufim-build work/stage1/rufim-source \
  --native-file=work/stage1/rufim-native.ini \
  --buildtype=debug --wrap-mode=nofallback \
  -Ddebugger=disabled -Dopengles=disabled \
  -Dpkg_config_path=/opt/homebrew/opt/libffi/lib/pkgconfig
ninja -C work/stage1/rufim-build -j4
```

## 提供統一比較流程的入口與限制

**本 agent 不執行 binary。** 後續由 root 的統一 runner 使用上述絕對 binary 路徑。

- **工作目錄**建議設成 `work/stage1/rufim-source`，讓未安裝的本地 `shaders/`、`fonts/` 被找到。shader 先查相對路徑再查編譯進去的 `/opt/homebrew/share/xsystem4`；字型順序是顯式指定→安裝路徑→本地預設。為避免兩版本意外使用不同的已安裝字型，runner 可對雙方明確傳同一組 `--font-gothic`／`--font-mincho` 絕對路徑。
- **不要帶 `--nodebug`**：debugger disabled 時此 CLI 選項不存在。runner 應依本次 build 的 CLI，而非直接沿用舊 launcher。
- **使用獨立存檔／home**：Rufim 的 PartsEngine save schema version 為 14、使用者 WIP 為 3；`--save-folder` 與 `XSYSTEM4_HOME` 都應指向此次比較的独立目錄。正常關閉可能觸發自己的 suspend/resume 流程，故退出也是會寫狀態的路徑。不要指向既有遊戲 SaveData 或舊 project state。
- **中文編碼是已確認的差異，尚未證實本機症狀**：Rufim 主程式與 pin 的 libsys4 沒有使用者的 GB18030 探測／解碼符號；`src/text.c:514` 仍以 SJIS_2BYTE 前進，`src/util.c:113` 的 unix_path 使用 sjis2utf。字型含中文字不代表 GB18030 byte stream 已正確解碼。若中文資料失敗，需區分「編碼／資源路徑」與「v14 VM 功能」，不能直接判成 Rufim 的 Dohna 架構無效。
- **macOS 呈現保持原碼**：`src/video.c:247–249` 仍要求 GL 3.1 core，沒有新增 Apple forward-compatible attribute；尚未測試 SDL context 建立／真正視窗呈現。若 runner 在這一步失敗，應先保存 baseline 結果，再另做明確標註的 macOS 相容性變體，避免混淆成 VM 比較結果。
- **測試輸入不同**：Rufim 有 `XSYS4_TEST_INPUT=<fifo/file>`，支援 `click x y`、`key RETURN`、mousedown/mouseup/move；不同於 WIP 的 XSYS4_AUTO_CLICK_SEQ。先以同一套實際 UI 操作比較，若需要 deterministic injection 再個別對齊語意。
- **错误可被容忍**：`System_Error` 預設印 log 然後繼續。觀察 `*GAME ERROR*`／`*WARNING*`，不能只以 process 活著判定成功。診斷變體可使用 `XSYS4_STOP_ON_GAME_ERROR`、`XSYS4_STRICT`；這些會改停止條件，應與 baseline 分開記錄。
- **可比較的最小門檻**：先 startup→實際可見視窗→標題→NewGame→正常等待輸入；若卡在 GL／CN 編碼，就保留阻塞證據，不繼續宣稱已完成對話、APEG 或存檔驗證。

## 留存檔案

- `rufim-build-manifest.json`：commit、binary SHA、options、file/lipo/otool/git 查核結果。
- `rufim-dependency-versions.json`：依賴辨識值。
- `rufim-native.ini`：固定工具路徑。
- `rufim-build-compat.patch`：唯一原始碼修正。
- `rufim-meson-setup.log`／`rufim-meson-brewffi-setup.log`：初始與 cache 問題记录。
- `rufim-meson-toolchain-setup.log`：最終正確工具鏈。
- `rufim-build-vanilla.log`：Bison 2.3 失敗。
- `rufim-build-fixed-toolchain.log`：正確工具鏈下 util.c 宣告失敗。
- `rufim-build-patched.log`：成功建置。
- `rufim-build-verification.log`：只讀非執行檢查。

本階段只能確認「可重建的 arm64 比較程式已備妥」。真實視窗、中文 Dohna 與 gameplay 正確性尚待 root 統一實測。
