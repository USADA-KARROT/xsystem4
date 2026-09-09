> 歷史查證／實測快照；目前交接狀態請先讀 [STATUS](../STATUS.md)。公開附件的本機路徑已去識別化。

# xsystem4 第一目標實測報告

2026 年 9 月 9 日｜最新 WIP 重建、故障重現、Rufim 對照｜macOS arm64

## 結果

**第一目標已完成：兩條來源已隔離重建，主要故障已有本機重現證據，下一階段有明確修復入口。這仍不是初步可玩測試版。**

這次最重要的結果是：你的最新 WIP 可以顯示真正的遊戲視窗並進入開場；原本「灰窗／看不到選單」包含多種問題，不能全歸因於 macOS 顯示。獨立設定為 1280×720 後，完整標題與右側選單出現，但點擊、對話等待及記憶體問題仍在。記憶體檢查器另外抓到 `GetPartsCGName` 將 `0x1` 當成字串指標的確定錯誤，Rufim 有可對照的介面適配寫法。

| 本輪確認 | 結果 |
|---|---|
| 最新 WIP 重新編譯 | 成功；引擎及 libsys4 原始碼無修改 |
| 最新 WIP 加 ASan／UBSan 重新編譯 | 成功；內建測試 2／2 通過，但遊戲執行觸發 UBSan |
| Rufim 比較版本重新編譯 | 成功；只補 `util.c` 的 `vm.h` 標頭引用 |
| 真正 macOS 視窗 | 已目視確認彩色標題、中文選單與開場美術 |
| 原預設 800×600 | 標題右側、下緣內容被裁切；1280×720 診斷設定可恢復完整構圖 |
| 實際點擊「新遊戲」 | 800×600 與 1280×720 都出現灰／黑轉場內容，仍在標題場景 |
| 引擎內建單次開始點擊 | 可到開場；沒有後續點擊仍輸出 MSG 2～40，共 39 則訊息 |
| 中文對話顯示與等待 | 尚未通過；1280×720 下也未在擷取的對話場景看到文字 |
| 記憶體／介面問題 | 確定重現字串指標讀取錯誤；另重現 string 與 VM_PAGE 重複釋放警告 |
| Rufim 直接執行相同中文資料 | 約 2.25 秒在開場初始化斷言失敗，未到標題 |

## 1. 重建的是哪個版本

| 項目 | 版本／條件 |
|---|---|
| 你的最新工作版本 | `484f4bc128c6f62fa3cba2bf3d75dddf5d4e465b`；2026-07-09 停工交接點 |
| WIP 配套 libsys4 | `8c939465910499b4802ec6dd619794ca58ba4708` |
| Rufim 比較版本 | `589cf2c7599761e30fc7b9a48ef6d8e106ef76df` |
| Rufim 配套 libsys4 | `703493c702cff32750a5f48f82b6c19e018d43b1` |
| 遊戲 AIN SHA-256 | `beefa6677237424a9e6dfb304299e5d47c524fb2956e1dd80aed113133fd8947` |
| 本機環境 | macOS 26.6.2、Apple clang 21、arm64、Meson 1.11.1、Ninja 1.13.2 |
| 關鍵相依 | SDL2 2.32.10、Homebrew libffi 3.5.2、Bison 3.8.2、現有 FFmpeg 62 系列 |
| 編譯設定 | debug；debugger disabled；desktop OpenGL；sanitizer 版本另開 address,undefined |

系統內建 Bison 2.3 無法處理 parser；改用已安裝的 Homebrew Bison。libffi 也明確使用 Homebrew 版本並驗證實際動態連結。未安裝新套件。

WIP 一般版與 sanitizer 版的 hashtable、instruction-width 測試各 2／2 通過。這些是 libsys4 的窄範圍測試，不涵蓋遊戲、PartsEngine、對話、存檔或記憶體生命週期。Rufim 的此份 Meson 建置回報「No tests defined」，不列為測試通過。

一般 WIP binary SHA-256：`d790b0d4ce53be3719de899ae3c3d99a6bca3e60589d7a6c7fb3cedfb5cd17e4`。sanitizer 版：`96f74c47eb6bf0decc005373a29e89f9794ee5e11b2b44dfe1674f41b2d26e2e`。Rufim：`38777ba479515eada360fbae5751591d5794f6a1b3e2c85899c8209de16ff367`。

## 2. 可見畫面與解析度：已排除一部分誤判

### 800×600 的實際視窗

（畫面附件保留於本機：原預設解析度的標題，右側與下方被裁切）

此圖來自真正 macOS 視窗。原始 `AliceStart.ini` 沒有 `ViewWidth`／`ViewHeight`，WIP 的預設值是 800×600。這解釋了本輪標題右側選單看不到的一部分原因。

### 只改隔離測試設定為 1280×720

（畫面附件保留於本機：1280×720 下完整標題與右側選單）

沒有改引擎原始碼；另外建立測試 ini 加上兩個尺寸設定，原 `AliceStart.ini` 保持原樣。完整美術、右側配置／指南／退出等選單隨即出現。

Rufim 的 `src/system4.c:343` 起已有依新 message API 判斷世代、在 ini 缺少尺寸時採 1280×720 的處理。這是值得選擇性移植的具體差異；對你的遊戲應先做受限適用條件，避免改變其他舊遊戲的預設。

### 點擊後的灰畫面，內部也同樣是灰的

（畫面附件保留於本機：實際滑鼠點擊後的視窗）

（畫面附件保留於本機：同一輪的內部 main_surface 擷取）

在可見「新遊戲」區域送出一次實際滑鼠點擊後，畫面變成灰色與右側黑色斜條。此輪內部截圖也有相同構圖，VM heartbeat 仍在 `SceneTitle@Run`。1280×720 實際點擊試驗亦有灰／黑轉場異常。

因此，**這次不能將灰畫面直接定性為 SwapWindow／Cocoa 沒有顯示正常 framebuffer**。WIP 與 Rufim 的核心 framebuffer blit→SwapWindow 程式相同，沒有現成的 Rufim macOS 顯示修正可以直接搬過來。

為取得桌面證據，把原 binary 原封不動複製進測試 `.app`。未封裝 binary 也能執行與產生內部截圖，但視窗工具無法辨識它；本輪沒有對未封裝路徑取得同等桌面證據，故不能宣稱「封裝 App 已修好灰窗」。

## 3. 對話自走：已重現，且不只涉及裁切

`single-start` 使用引擎原有測試功能，只在啟動約 20 秒送出一次 `(85,82)` 點擊，其後不再送出輸入。程式進入 `DohnaDohna@RunGame`／`SceneAdv`，訊息從 MSG 2 到 40 連續前進。`resolution1280-single` 在完整解析度下推進到 MSG 42，共 41 則訊息。

（畫面附件保留於本機：完整解析度下的開場對話場景，當時訊息正在推進但沒有可見對話文字）

這證實「訊息執行到該處」與「玩家看得到文字、能控制前進」是不同門檻。主選單文字可見，仍不足以證明 ADV 動態中文排字已通過。log 的訊息輸出仍有編碼亂碼，亦不能只據此斷定畫面字型錯誤。

白色電視畫面本身不是故障判定：既有原版比較已指出該開場可以白屏。這次失敗的驗收點是未觀察到正常對話文字與逐次點擊等待，而非單純看到白色。

實際滑鼠點擊和內建測試點擊結果不同，仍須保留這個差異。WIP 的測試點擊直接覆寫遊戲座標，且 override 沒有自動清除，所以不能用它證明正常滑鼠座標與按鍵流程已正常。

靜態追查還發現 `SDL_GetMouseState` 的視窗座標直接混用由 drawable 尺寸算出的 viewport；當兩者比例不同時公式會錯。Rufim 也保留此公式。但本輪沒有量到該視窗的實際 DPI 比例，不能宣稱此次灰轉場已證實由 Retina 兩倍縮放造成。

## 4. 已抓到的確定故障與下一個修復入口

### 優先處理 GetPartsCGName 的 HLL 介面

最新 WIP sanitizer 版本在約 **48.605 秒**退出，exit code **134**。它不是被 100 秒上限停止。UBSan 報告：

```text
parts/parts.c:1279:7: runtime error:
load of misaligned address 0x000000000001 for type 'struct string *'
PE_GetPartsCGName → ffi_call → hll_call → vm_execute
Current function: parts::detail::CCGParts@CGName::get
Caller: AdvEventCg@Set → SceneAdv@Execute
```

WIP 綁定的是舊形式：`void GetPartsCGName(int parts_no, string **out, int state)`。Rufim 已有新形式適配器：`string *GetPartsCGName_ix(int parts_no, int state)`，並依 AIN 的回傳型別選擇綁定。

這不是單純參數換順序；舊形式的 out 參數在新形式中已改成回傳值。已直接只讀核實 AIN：PartsEngine library 27／function 699 為 `Parts_GetPartsCGName`，回傳字串、參數為 Number:int 與 State:int；fno 9648 的 `0x2FC070` 正是 `CALLHLL 27 699 0`，與故障位址完全吻合。當次 `state=1` 被舊 C 介面解讀為 `out=0x1`，**介面錯配已確認**。下一階段應按簽名選適配器，同時保留舊遊戲的介面；不宜只新增空指標檢查、吞掉錯誤或強迫繼續。

宣告、callsite、綁定與對照修補詳見[ABI 故障分析](../evidence/stage1/abi-analysis.md)及[AIN 原始查證輸出](../evidence/stage1/ain-hll-signature-evidence.txt)。Rufim 對應提交為 [1f312c5](https://github.com/Rufim/xsystem4/commit/1f312c5db8603705c18fa9d18aefcb1c7e265c34)。

### 記憶體警告仍然存在

- sanitizer 輪出現 `Double free of string object (ignored)`，隨後才遇到上述 UBSan 故障。
- 1280×720 一般版輪出現兩次 `double free of slot 498256 (VM_PAGE)`。
- 新遊戲還有 `Personality.jaf:27` 的 `id != ""` 斷言，以及 `X_ASSIGN` 超出 page 後被截短的警告。

一般版能繼續跑、或正常收到終止要求後回傳 0，不代表以上警告已被解決。string／VM_PAGE 警告與這次指標錯誤是否有共同根因，尚未證明。

### 對話等待的有限修復方向

靜態 trace 已追到 `Join → EraseEndTask → IsEndWaitSection → EndWaitForClick`。需要記錄哪個 motion 或 section 被提早移除，不應直接禁止 Observer callback。

可優先檢查兩個具體缺口：巢狀 `EraseAll → IsExist` 會覆寫外層 HLL 型別上下文，影響元素清理；CASTimer constructor reset 分支在目前 flag 分派下不可達。不過 motion task 直接使用 RCASTimer，因此 CASTimer 旗標缺口尚不能視為自走的已確認根因。細節與行號見[對話分析](../evidence/stage1/dialogue-analysis.md)。

## 5. Rufim 對照結果

相同中文遊戲資料、獨立 home／存檔、固定來源與配套 libsys4，Rufim 約 **2.247 秒**以 exit code **1**停止。最後是：

```text
Assertion failed at CActivityWrap.jaf:20: (nonnull) m_root
CActivityWrap@Root::get → SceneContext@Create → SceneLogo@0
```

前面還有 motion easing predicate 找不到元素、中文字串／路徑亂碼等訊息。它沒有到標題，這次也沒有實測到其 APEG、對話、存檔或後續玩法。

**建議沿用你的 WIP 為基底，逐項移入已核對介面的解法。** Rufim 的 v14 `GetPartsCGName` 適配器和尺寸預設有直接參考價值；Rufim 整版目前不能直接取代你的 macOS 中文版本。其 initialization assertion 與中文解碼／closure 問題的因果尚需另外定位。

## 6. 測試紀錄與可重現性

所有測試在獨立 APFS 遊戲複本、全新 home 與全新 save 目錄執行。沒有將原存檔帶入 Rufim，也沒有混用兩種 PartsEngine 存檔格式。

| Run ID | 條件 | 關鍵結果 |
|---|---|---|
| baseline-visible | 未封裝 WIP、無輸入 | 內部畫面與 heartbeat；桌面工具無法辨識程序 |
| bundled-visible | 相同 binary 的 App、無輸入、240 秒上限 | 真正彩色標題；到上限由 runner 停止 |
| dialogue-visible | 800×600、一次實際滑鼠點擊 | 灰／黑內容，內外截圖一致；仍 SceneTitle |
| single-start | 800×600、20 秒時一次內建點擊 | 開場，MSG 2～40，另有斷言；由操作者停止 |
| sanitizer-start | 相同 WIP、ASan／UBSan、一次內建點擊 | 48.605 秒 UBSan，exit 134 |
| baseline-cn | Rufim、相同中文資料、無輸入 | 2.247 秒初始化斷言，exit 1 |
| resolution1280 | WIP、獨立 1280×720 ini、實際點擊 | 完整標題與右側選單；點擊轉場問題仍在 |
| resolution1280-single | WIP、1280×720、一次內建點擊、120 秒上限 | MSG 2～42；未見正常對話文字；VM_PAGE 警告；到上限停止 |

測試命令、binary hash、來源 SHA、開始／結束時間、退出原因、環境開關及原始 log 均收在[實測證據](../evidence/stage1/實測證據.json)與其 `runs` 子目錄。`XSYS4_STOP_ON_GAME_ERROR=1` 已明確記錄；各來源對斷言／錯誤的處理仍不同，不能僅比較退出碼。

原最新 WIP 仍在同一 commit，只有原已存在的 `subprojects/.wraplock`；原主 repo 仍只有 `.DS_Store` 修改。原工作遊戲與測試副本的 AIN、AIN 備份、AliceStart.ini、EX、Pact、Version 六檔 hash 均與起始基準相同。沒有修改原引擎或原存檔。

## 7. 下一步建議與驗收

**下一階段先修 v14 HLL 簽名，再處理輸入／對話等待。** 首個 patch 應以 `GetPartsCGName` 為界，不直接整合整份 Rufim。

1. 核對並適配 `GetPartsCGName` 新舊形式；用本輪 sanitizer 腳本越過相同 `AdvEventCg@Set` 場景，確認不再讀取 `0x1`，並回歸舊 out 參數形式。
2. 將 1280×720 的適用條件固定；量測 window／drawable／logical 尺寸與實際滑鼠座標，建立一次真實點擊可可靠開始遊戲的門檻。
3. 針對首個 Join collection 記錄 timer、motion、section、predicate 結果及巢狀 HLL 上下文；修正後要求首段中文對話無輸入停留至少 20 秒、一次點擊只前進一頁。
4. 再清查已出現的 string／VM_PAGE 生命週期問題與 `Personality` 斷言，才往基本存讀檔、前段玩法擴張。

第一目標至此提供了可重建基準、可重現的失敗與修復入口。第二目標尚未完成；第三目標「初步可玩測試版」仍需實際點擊、對話及基本存讀檔等驗收。

## 8. 本機重跑與交付

可執行第一目標－重新測試.command（完整附件保留於本機），啟動 1280×720 的診斷版，無自動點擊、每次建立新存檔，最長 5 分鐘。它依賴本次 `work/stage1` 建置及這台電腦已安裝的函式庫，並非可攜式發布包。它不會套用下一階段修復。

重跑 sanitizer 故障的命令（先切換至本任務工作目錄；每次 run ID 必須唯一）：

```sh
python3 work/stage1/scripts/run_engine.py wip-asan check-abi-001 \
  --seconds 100 --bundle --click-seq '20000,85,82'
```

建置細節：[Rufim 建置紀錄](../evidence/stage1/rufim-build-review.md)、[WIP sanitizer 建置紀錄](../evidence/stage1/wip-asan-build-review.md)、[呈現流程分析](../evidence/stage1/present-analysis.md)、[滑鼠座標分析](../evidence/stage1/input-scaling-analysis.md)。上一份完整上游差距報告（完整附件保留於本機）仍保留；本報告新增的是實機驗證，並更新其中尚未實測的判斷。

額度：本階段起始查詢為本週已用 7%；整理交付時最新查詢為 16%，尚餘約 84%，本階段差值約 9 個百分點。這是帳號共享配額的百分比差值，不是精確 token 數；本輪沒有使用重置額度。
