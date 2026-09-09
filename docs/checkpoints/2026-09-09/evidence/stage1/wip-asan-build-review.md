# WIP ASan／UBSan 隔離建置

日期：2026-09-09。**建置成功，Meson 內建測試 2／2 通過，原始碼未修改。** 本 agent 沒有啟動 xsystem4 遊戲或操作 UI。

## 來源與產物

| 項目 | 值 |
|---|---|
| Source directory | `work/stage1/wip-source`，沿用此次統一比較的同一份來源 |
| 主倉庫 commit | `484f4bc128c6f62fa3cba2bf3d75dddf5d4e465b` |
| libsys4 commit | `8c939465910499b4802ec6dd619794ca58ba4708` |
| Build directory | `work/stage1/wip-asan-build` |
| Binary | `work/stage1/wip-asan-build/src/xsystem4` |
| Binary SHA-256 | `96f74c47eb6bf0decc005373a29e89f9794ee5e11b2b44dfe1674f41b2d26e2e` |
| Binary format | Mach-O 64-bit executable arm64；file／lipo 均確認 |
| Source change | 無 tracked source diff；libsys4 工作樹亦 clean |

建置前後都有既存的未追蹤 `subprojects/.wraplock`，本次未刪除或修改此檔。沒有安裝套件、fetch／merge 分支、加入遊戲修補。

## 工具鏈與實際 instrumentation

- macOS 26.6.2、Apple clang 21.0.0 (clang-2100.1.1.101)、ld64 1267，原生 arm64。
- Meson 1.11.1、Ninja 1.13.2、Homebrew Bison 3.8.2／Flex 2.6.4。
- `wip-asan-native.ini` 固定 CommandLineTools clang、Homebrew Bison／Flex／pkg-config。
- Meson options：`--buildtype=debug --wrap-mode=nofallback -Ddebugger=disabled -Dopengles=disabled -Db_sanitize=address,undefined`。
- 明確設定 `-Dpkg_config_path=/opt/homebrew/opt/libffi/lib/pkgconfig`；setup 確認 libffi 3.5.2，otool 實際連結 `/opt/homebrew/opt/libffi/lib/libffi.8.dylib`。
- SDL2 2.32.10，libavcodec 62.28.101、libavformat 62.12.101、libavutil 60.26.101、libswscale 9.5.101；OpenGL 使用 macOS framework。
- 編譯為 `-O0 -g`。`compile_commands.json` 的 vm.c、heap.c 與測試來源均有 `-fsanitize=address,undefined`；binary 連結 `libclang_rt.asan_osx_dynamic.dylib`，nm 可見 ASan／UBSan instrumentation 符號。

Meson 提醒 Clang sanitizer 與 b_lundef 搭配可能有問題；本機實際編譯、連結與測試均成功，因此沒有為此改旗標或程式。現有來源 warnings 保留於 build log，未擴大修改。

可重建指令（從本任務 workspace 執行；首次 setup 用以下指令，既有目錄應使用 reconfigure）：

```sh
PKG_CONFIG_PATH=/opt/homebrew/opt/libffi/lib/pkgconfig meson setup \
  work/stage1/wip-asan-build work/stage1/wip-source \
  --native-file=work/stage1/wip-asan-native.ini \
  --buildtype=debug --wrap-mode=nofallback \
  -Ddebugger=disabled -Dopengles=disabled \
  -Dpkg_config_path=/opt/homebrew/opt/libffi/lib/pkgconfig \
  -Db_sanitize=address,undefined
ninja -C work/stage1/wip-asan-build -j4
ASAN_OPTIONS=halt_on_error=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  meson test -C work/stage1/wip-asan-build --no-rebuild
```

## 測試結果與涵蓋範圍

Meson test exit code：0。測試時 ASan 設為遇錯停止／abort，UBSan 設為遇錯停止並輸出 stack trace。

| 測試 | 結果 | Return code | 時間 | 主要涵蓋 |
|---|---|---|---|---|
| `libsys4:hashtable` | OK | 0 | 0.54 秒 | 整數鍵新增／查詢／刪除、不存在键、重複刪除、bucket compaction、多輪 churn |
| `libsys4:instructions` | OK | 0 | 0.65 秒 | NEW／CALLHLL／S_MOD／OBJSWAP／DG_STR_TO_METHOD 在 v14、v11、切回 v4、再回 v14 的 nr_args 與 ip_inc 一致性 |

這兩項測試沒有 sanitizer 診斷，但沒有執行遊戲 VM 主流程、對話、顯示、save/resume 或既知 VM_PAGE double-free 最小重現。不能將 2／2 通過解讀為遊戲記憶體問題已排除；也未宣稱完成 LeakSanitizer 檢查。

## 交付統一 runner

使用完整 build directory 中的 binary，保留其旁邊的動態依賴與工具鏈 runtime 搜尋路徑。工作目錄及獨立 `--save-folder`／`XSYSTEM4_HOME` 應與此次普通 WIP 對照一致；`debugger=disabled` 時不可帶 `--nodebug`。

建議初次重現沿用上列 ASAN_OPTIONS／UBSAN_OPTIONS，記錄完整錯誤 stack 與觸發步驟。Sanitizer 會增加執行時間與記憶體成本，因此此 binary 用於定位錯誤，不適合直接與普通 debug binary 做 frame-time 效能比較。

## 留存證據

- `wip-asan-build-manifest.json`：來源、SHA、旗標、compile 範例、非執行 binary 檢查與精簡 test 結果。
- `wip-asan-source-before.json`：建置前 git 狀態。
- `wip-asan-native.ini`：固定工具路徑。
- `wip-asan-meson-setup.log`／`wip-asan-build.log`：configure／build 記錄。
- `wip-asan-build-verification.log`：file、lipo、otool、source status 與 sanitizer symbols。
- `wip-asan-tests-summary.json`／`.log`：只保留測試名稱、結果、return code、時間和測試輸出，沒有複製 Meson 全環境。

Meson 自行產生的原始 testlog 保留於 build 目錄，未將其中完整環境複製到此報告或摘要。
