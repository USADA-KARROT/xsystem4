> 歷史查證／實測快照；目前交接狀態請先讀 [STATUS](../STATUS.md)。公開附件的本機路徑已去識別化。

# xsystem4 專案進度、上游差距與復工建議

查證日期：2026-09-09，台灣時間。對象：USADA-KARROT/xsystem4、本機 Dohna Dohna macOS／中文移植、nunuhara 官方主線、libsys4，以及相關公開工程師分支。

## 1. 結論與建議

**你的成果仍在，而且最新進度已推送到 GitHub；目前應定位為「已有大量底層實作，但尚未通過實際遊玩驗收的移植原型」。** 真正停工點是 **2026-07-09 12:00:31（台灣時間）**，最新分支為 `wip/post-checkpoint-2026-07-06`，提交 `484f4bc`。GitHub 首頁 `master` 停在 7/6，單看首頁會低估你的進度。[停工交接提交](https://github.com/USADA-KARROT/xsystem4/commit/484f4bc128c6f62fa3cba2bf3d75dddf5d4e465b)

最重要的六項判斷：

1. **你缺少官方上游 47 筆提交，當中 45 筆是在停工後納入主線；另 2 筆在停工前已存在但沒有同步。** 47 筆包含 16 筆 merge，實質非 merge 提交是 31 筆；停工後部分則是 30 筆非 merge，不能把 merge 再當成一項功能。
2. **你的最新工作線有 23 筆上游沒有的提交。** 它已以較新的上游為底重整，與首頁 master 的「自己獨有 157／上游獨有 162」是兩組不同數字，不能混用。
3. **目前最先阻擋遊玩的，是實際 macOS 視窗灰色、對白自動跳過與文字顯示未驗收。** 最新交接文件明確更正：早期成功畫面是引擎內 framebuffer 截圖，不能證明 SDL 視窗真的顯示正常。另有 VM_PAGE double-free，存讀檔和 TV 之後的核心玩法尚未完整驗證。[最新停工狀態](https://github.com/USADA-KARROT/xsystem4/blob/484f4bc128c6f62fa3cba2bf3d75dddf5d4e465b/PORT-STATUS-2026-07-09.md)
4. **官方這段期間主要推進 Rance IX、PartsEngine、文字、音訊／影片、存檔與穩定性。** 官方相容性表已把日文 Rance IX 列為 Supported，但英文版仍 Unknown，Rance IX 之後的作品仍 Unsupported；沒有正式宣布 Dohna Dohna 已支援。[官方相容性表固定版本](https://github.com/nunuhara/xsystem4/blob/fa09b0059f3ffcc7b9e853f330e832ea75d74502/game_compatibility.md)
5. **另一條非常相關的新進度在 Rufim 的公開實驗分支。** 它在你停工後增加大量 v14／Ixseal、Parts、存讀與 APEG 解碼實作。這值得安排獨立比較，不能等同於官方已合併，也不能僅憑提交文字認定已在你的中文 macOS 環境可玩。
6. **建議下一步先恢復可靠的視窗與對話驗收基準，同時對 Rufim 做有限範圍的技術比較；之後分批納入官方修正。** 直接更新 master、整批覆蓋子模組或合入數百筆實驗提交，會讓既有 v14／中文差異更難追查。

## 2. 查證方式與可信度

本次實際做了：本機檔案與 Git 狀態盤點；在本任務的 `work` 目錄獨立 clone／fetch 公開儲存庫；以 GitHub 即時 API 取得 branches、PR、issues、releases、forks；以完整 Git 歷史計算共同祖先與差異；閱讀關鍵 diff；對官方主線及 libsys4 做 `git merge-tree --write-tree` 靜態合併試驗。

**沒有修改你原有專案程式、切換原分支、合併、推送、啟動遊戲或重跑建置／遊戲測試。** 本文的 PASS、畫面、崩潰與遊戲場景結果，均明確指向 7 月既有紀錄；今日新增的是版本、程式差異與紀錄一致性的查證。

時間一律轉為台灣時間。停工後更新依提交**首次納入官方 master 的時間**分類，不單看作者日期。例如 3D debugger 的程式早在 5 月撰寫，7 月才合入，仍屬停工後可取得的主線更新。網頁有快取時以即時 API／Git refs 為準。

「所有工程師」在本報告指可公開查得的官方提交與 PR、直接依賴 libsys4、公開 fork 活動；不包含私人分支、未推送的工作或不可見的內部討論。公開 fork 列表此次回傳 12 個，包含你自己；逐一檢查最後推送時間，再深入檢查停工後仍活躍的外部 fork。

## 3. 你的實際版本與本機檔案

| 位置／分支 | 查得版本 | 實際意義 |
|---|---|---|
| GitHub 預設 `master`／本機 `<USER_HOME>/xsystem4-dev/xsystem4` | `917f1a2`，7/6 14:04 | 舊開發線，最後修 GetGameFolderPath 的 v14 字串回傳簽名；不是最新移植成果 |
| `cn-on-upstream` | `02f061c`，7/6 21:52 | 重整後 checkpoint；歷史上驗到標題、NewGame、開場 CG、TV 的內部渲染 |
| `wip/post-checkpoint-2026-07-06` | `484f4bc`，7/9 12:00 | 真正最新，含 7 個 checkpoint 後程式修改和 1 個停工文件提交，已在遠端 |
| `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/worktrees/xsystem4-cn-on-upstream` | 同上 `484f4bc` | 最新實體工作目錄；程式碼無未提交差異，僅 `.wraplock` 未追蹤檔 |
| `<USER_HOME>/xsystem4-dev/xsystem4-upstream` | `e8bd5ab`，7/6 02:23 | 本機上游副本仍停在 7 月；不代表今天的官方上游 |
| 最新移植配套 libsys4 | `8c93946`，7/6 16:13 | worktree 與主專案 gitlink 相符，另有獨立 libsys4 worktree |
| 本機原 fork 的 libsys4 | `0ca08d6` | 舊 `v14-dohnadohna` 線，勿與最新 `cn-on-upstream` 混用 |
| 官方 xsystem4 現在 | `fa09b00`，8/26 09:48 | 最新納入跳讀時動畫加速 |
| 官方 libsys4 現在 | `cbc57ce`，8/10 09:54 | 也是官方 xsystem4 現在所 pin 的版本 |

本機主目錄另有 `.DS_Store` 修改；上游工作目錄有 `.wraplock` 未追蹤檔，沒有因此找到更新的未提交程式。`dohnadohna-mac-port` 本身是協調資料夾，不是獨立 Git repo；它保存 plans、reports、logs、screenshots、builds、patches、runtime-data 與 worktrees。查得 xsystem4 無 stash。

停止時間有三重佐證：最後程式提交 `d3cd045` 在 7/9 09:14:30；最後查得的 runtime log 修改於 11:57:59；12:00:31 建立明訂 project pause 的交接提交，12:00:33 推送。這比依檔案夾 mtime 或 GitHub 首頁推定可靠。以日期差計，7/9 到 9/9 是 62 天。

早期 `<USER_HOME>/Downloads/文件/文件_文字/xsystem4-briefing-v2.md` 修改於 2/2，描述的 constructor／heap bottleneck 已經過時，適合保留為歷史背景。

### 最新 checkpoint 後，你已經做了什麼

| 提交 | 完成的工程修改 | 目前限制 |
|---|---|---|
| [6e66a1e](https://github.com/USADA-KARROT/xsystem4/commit/6e66a1e) | ResumeSave 真正寫檔，移除 2 秒內回報假成功的節流 | 未證明 Load round-trip；高頻快照成本仍須量測 |
| [1dae88b](https://github.com/USADA-KARROT/xsystem4/commit/1dae88b) | 移入 voice、Kiwi、CGManager、Parts construction／panel／movie 等 HLL，以及大量 prelink stubs | 提交明訂 WIP／未驗證；文件記 594，實際 594 行的 prelink 檔有 593 個註冊呼叫，均不能當完成 API 數 |
| [69000c6](https://github.com/USADA-KARROT/xsystem4/commit/69000c6) | v14 Parent getter 支援 pending parent、無 parent 回 0 | 已修過 SceneLogo Attach loop，不應再照舊交接從頭二分 |
| [cbf5774](https://github.com/USADA-KARROT/xsystem4/commit/cbf5774) | 修 value globals 殘留 -1 使 skip 自動開啟 | 只是其中一條自動推進原因；後續仍有 Motion 等待問題 |
| [2b8597e](https://github.com/USADA-KARROT/xsystem4/commit/2b8597e) | CN 的 `_MSG` 經具名 message handler 進入文字 model；加單函式 trace | model／composer 有資料不代表視窗已顯示 |
| [d6af8eb](https://github.com/USADA-KARROT/xsystem4/commit/d6af8eb) | Array 依元素型別決定 2-slot stride，避免 struct id 誤用 | 仍需包含 interface／wrap／存檔回復的回歸 |
| [d3cd045](https://github.com/USADA-KARROT/xsystem4/commit/d3cd045) | CASTimer 建構時重設 epoch，避免 slot 重用繼承舊時間 | Motion Join 引起的每頁立即結束尚未解決 |

## 4. 到底做到哪裡：工程進度與遊玩完成度

| 領域 | 已有證據 | 尚缺驗收 |
|---|---|---|
| 建置／libsys4 | arm64 建置紀錄；v14 指令寬度、hashtable、位移 UB、字串尾端越界修正；既有一般及 sanitizer 單元測試各 2 PASS／0 FAIL | 不是完整遊戲或最新 WIP 的安全證明 |
| VM／FFI | v14、delegate、2-slot、GB18030 啟動、部分 HLL ABI 已移植 | heap lifetime、泛型／interface 跨路徑與完整回歸 |
| 中文／pactex | 可讀 195 個 pactex 的歷史報告；GB18030、CG、activity 與內部畫面 | 中文字真的進可見視窗、文字排版／完整標題 |
| 畫面／輸入 | 歷史內部畫面達標題→NewGame→開場 CG→TV；DOWN click／queue 已有實作 | **實際 SDL 視窗灰色**，為當前最先處理的阻擋項 |
| 對話 | `_MSG`→message→model→composer 已追到；自走回呼有 trace | 正常逐句顯示、等待、NEXT／姓名／對話框 |
| 記憶體 | 舊短路徑 sanitizer 結果、部分 refcount 修補 | 7/9 raw log 仍有同 slot VM_PAGE double-free |
| 音訊／影片 | voice、Kiwi、PartsMovie 等 WIP；APEG fallback 有抽取 SOND/Ogg 音訊 | 本 fork 的 fallback 並未完成 APEG 影像解碼；音畫同步仍待驗 |
| 存讀／backlog | ResumeSave、部分持久化程式存在 | 存檔→離開→重啟→讀檔、設定、backlog 的完整往返 |
| 核心玩法／交付 | 測試啟動與 trace 工具有保存 | TV 以後正式遊戲迴圈、長跑、跨平台、可交付 beta |

**不提供完成百分比。** 74 個檔案、約 +18,386／−1,574 行的自有改動，或 Wave 0–3 的歷史打勾，都不能可靠換算剩餘工作量。

還有兩個需要更正的舊認知：

- **TV 白畫面本身不等於解碼故障。** 7/8 Wine 原版基準顯示新聞收尾也會白，當時真正缺的是對話框、NEXT 與等待點擊。因此 APEG 解碼值得補，但不能把 TV 白色當作它一定是首要根因。
- **舊 ASan 綠燈不涵蓋最新修改。** 本機 ASan binary 修改時間為 7/6 20:39；一般開發 binary 為 7/9 09:14。7/9 ADV 路徑仍出現 double-free，必須重新建置對應最新 SHA 的 sanitizer 版本才可驗證。

證據主要來自最新 PORT-STATUS，以及本機 `logs/tests/gui-run-log.md`、`logs/runtime/run-20260709-091121.log` 第 236–237 行、`reports/visual-parity/wine-baseline-index.md` 第 24–34 行；完整絕對路徑列於附錄。

## 5. 官方主線：停工後所有已納入更新

### 精確差距

| 比較基準 | 你的獨有提交 | 官方獨有提交 | 用途 |
|---|---:|---:|---|
| 首頁 master `917f1a2` 對官方 `fa09b00` | 157 | 162 | 舊線歷史，不能作最新專案狀態 |
| checkpoint `02f061c` 對官方 | 15 | 47 | 比最新 WIP 少 8 個自有提交 |
| 最新 WIP `484f4bc` 對官方 | **23** | **47** | 本報告主要比較 |
| 最新 libsys4 `8c93946` 對官方 `cbc57ce` | **6** | **20** | 相依套件必須一併處理 |

最新 WIP 與官方共同祖先是 `e8bd5ab`（台灣 7/6 02:23）。上游從共同祖先到今天修改 55 檔，+1,109／−389 行；這是官方那一側的變更量，不是合併後需手寫的行數。雙向 patch-id 比較未發現完全相同的獨有補丁，但不排除局部功能或邏輯已有等效實作。

### 哪些工程師做了什麼

| 工程師／帳號 | 停工後公開主線成果 | 解讀 |
|---|---|---|
| Nunuhara Cabbage／`nunuhara` | xsystem4 15 筆非 merge；合併下表 14 個 PR；另外 libsys4 2 筆非 merge | 特效、文字、記憶體、controller、Rance IX 效能、跳讀加速，以及維護整合 |
| `kichikuou` | xsystem4 15 筆非 merge，14 個已合併 PR；libsys4 5 筆非 merge | 3D、輸入、影片、BGM、存檔格式、相容性、資源與亂數等 |
| Dmitry Kazantsev／`Rufim` | libsys4 2 筆非 merge 已進官方；另維護大量未上游化 xsystem4 實驗工作 | 官方部分是 AFA ID 相容修正；實驗分支見下一節 |
| `brombrom-hani` | 本次停工後區間未查到新增官方提交 | 仍屬歷史貢獻者；不是此區間的新進度 |

這裡按非 merge 計工程提交，按 PR 計整合項目；兩者是同一工作不同視角，不加總為「成果數量」。作者與 GitHub 帳號只在公開資料能對應時列出。

### 停工後 14 個已合併 PR（全部由 kichikuou 提交、nunuhara 合併）

| 台灣合併日 | PR | 內容 | 對你的影響 |
|---|---|---|---|
| 7/14 | [#358](https://github.com/nunuhara/xsystem4/pull/358) | 3D debugger commands | 日後調查 3D 資源與 renderer 的工具 |
| 7/17 | [#360](https://github.com/nunuhara/xsystem4/pull/360) | NumLock 關閉時數字鍵盤輸入 | 獨立、低耦合的輸入修正 |
| 7/20 | [#361](https://github.com/nunuhara/xsystem4/pull/361) | 使用 libsys4 zlib wrappers | 需與 libsys4 更新配套，避免缺 symbol／header |
| 7/25 | [#362](https://github.com/nunuhara/xsystem4/pull/362) | pl_mpeg 接受 MPEG-2 program-stream header | 不等於原生 APEG 解碼 |
| 7/25 | [#363](https://github.com/nunuhara/xsystem4/pull/363) | FFmpeg audio frame 長度改用 nb_samples | 適合優先補入的音訊正確性修正 |
| 7/27 | [#364](https://github.com/nunuhara/xsystem4/pull/364) | Rance IX BGM loop | 共用 audio／mixer 有你的 voice 變更，需要整合 |
| 8/1 | [#365](https://github.com/nunuhara/xsystem4/pull/365) | DrawShadow mesh attribute、zero-power specular | 3D 兼容改善，非目前 Dohna 首要 blocker |
| 8/3 | [#366](https://github.com/nunuhara/xsystem4/pull/366) | PartsEngine 存檔格式穩定化 | 需保存舊檔並測試往返，不能只看寫檔成功 |
| 8/5 | [#367](https://github.com/nunuhara/xsystem4/pull/367) | 日文 Rance IX 列入支援 | 是官方遊戲驗收里程碑，不代表 Dohna 已支援 |
| 8/6 | [#368](https://github.com/nunuhara/xsystem4/pull/368) | 更新 libsys4 | 涉及資源格式／壓縮／解碼修正 |
| 8/9 | [#369](https://github.com/nunuhara/xsystem4/pull/369) | archive 路徑解析 | 有助資源定位；仍須保留你的編碼處理 |
| 8/12 | [#371](https://github.com/nunuhara/xsystem4/pull/371) | construction CG 依名稱參照；配套 save version 5 | 降低 CG 數字 ID 隨資源版本變更的問題 |
| 8/12 | [#372](https://github.com/nunuhara/xsystem4/pull/372) | 讀回多行文字時保留換行 | 直接影響存讀後文字正確性 |
| 8/14 | [#373](https://github.com/nunuhara/xsystem4/pull/373) | Math rand() 改 mt19937 | 亂數行為與測試改善，移入後應驗語義 |

### PR 以外的主要直接更新

| 時段 | 更新 | 對你的判斷 |
|---|---|---|
| 8/9 | [DAP 訊息 NUL 終結](https://github.com/nunuhara/xsystem4/commit/e699fb0) | debugger 安全性修正，可獨立處理 |
| 8/14–15 | [字寬取 ceil](https://github.com/nunuhara/xsystem4/commit/4e20a49)、[粗體寬度](https://github.com/nunuhara/xsystem4/commit/5cbc697)、[排版整理](https://github.com/nunuhara/xsystem4/commit/a19c200)、[字型快取重設](https://github.com/nunuhara/xsystem4/commit/5fb3c49) | 值得補，但必須保留 GB18030 的四位元組字元處理 |
| 8/15 | [多處 memory leaks](https://github.com/nunuhara/xsystem4/commit/5d032d7)、[Shaman's Sanctuary crash](https://github.com/nunuhara/xsystem4/commit/e3bf653) | 穩定性改進；不是已證明修掉你 VM_PAGE double-free |
| 8/15、8/24–26 | [上下 crossfade](https://github.com/nunuhara/xsystem4/commit/e76762d)、[vwave 改善](https://github.com/nunuhara/xsystem4/commit/bf3430d)、[vwave scroll](https://github.com/nunuhara/xsystem4/commit/b30a107)、[mosaic 修正](https://github.com/nunuhara/xsystem4/commit/4233c7d) | 視覺效果的功能缺口；應在基本視窗驗收後處理 |
| 8/17 | [RemoveController 回傳 delegate indices](https://github.com/nunuhara/xsystem4/commit/05820dc) | API 契約修正，與你的 controller／message 路徑相關 |
| 8/21 | [Rance IX UI 效能緩解](https://github.com/nunuhara/xsystem4/commit/135cae2) | 帶遊戲專屬條件；沒有證據表示會改善 Dohna |
| 8/26 | [SetSpeedupRateByMessageSkip](https://github.com/nunuhara/xsystem4/commit/fa09b00) | 實作「有意跳讀時的動畫加速」；不能視為你的「未點擊就自動翻頁」修正 |

另有停工前未同步的 [#357](https://github.com/nunuhara/xsystem4/pull/357)：缺少目標 texture 時 construction 應回傳失敗，而非繼續畫圖造成 framebuffer 錯誤。台灣 7/7 已合入，包含 1 程式提交和 1 merge。它有參考價值，但與「framebuffer 有圖、視窗灰」並非已證明同一根因。

## 6. libsys4：不能只更新 xsystem4 主庫

你的最新 pin `8c93946` 與官方 `cbc57ce` 共同祖先是 `ed74c9e`（台灣 5/18）。因此 libsys4 雖在 7/6 做過重整，基底仍比 xsystem4 主庫更舊。

缺少的 20 筆上游提交含 13 筆非 merge；其中 **停工後新增 13 筆（9 筆非 merge）**，另 7 筆早已存在。主要差距如下：

| 領域 | 官方更新 | 整合注意 |
|---|---|---|
| FLAT | CG metadata 修正、binary writers、flat_free_library | 其中部分早於停工，不能全算成這兩個月新增 |
| 指令 metadata | delegate 指令參數描述、讀取／建立 AIN 時初始化 metadata | 與你的 v14 `ip_inc`／指令寬度修正部分重疊，需語義整合 |
| 壓縮 | 可選 libdeflate backend、統一 zlib wrapper | 需一起移入 header／source／build 設定；沒有 libdeflate 時可退回 zlib |
| AJP | 缺 mask 時的無效 buffer read 修正 | 值得優先納入的讀檔正確性修正 |
| AFA | 重複或不正常 ID 的相容處理 | 7 月 #66 後又有 8/10 `cbc57ce` 部分回退以修 regression，不能只摘取早期一半 |
| QNT | 接受部分工具產生的奇數高度 alpha、移除多餘 padding | 更新後需用同一資源樣本做影像回歸 |

你的 6 筆獨有提交包含 v14 支援、`ht_remove_int`、hashtable pointer-size 修正、測試、little-endian 位移 UB 與截斷字元越界修正。不能直接把 submodule pin 指到官方 HEAD，就假設這些仍在。[你的 libsys4 分支](https://github.com/USADA-KARROT/libsys4/tree/8c939465910499b4802ec6dd619794ca58ba4708)、[官方 AFA regression 修正](https://github.com/nunuhara/libsys4/commit/cbc57ce5642dadbbeda3c4894e1c2da7e15dbbff)

相關工具另有一項對 macOS 值得留意的修正：kamin1ii 提出的 [libsys4 #70](https://github.com/nunuhara/libsys4/pull/70) 未合併，問題改由 kichikuou 的 [alice-tools #93](https://github.com/nunuhara/alice-tools/pull/93) 在台灣 8/12 合併，修正 libiconv 的 CP932 wave dash 往返轉碼。若你的流程會用 alice-tools 解包／重打包，應一併核對工具版本；這不是 xsystem4 runtime 已自動取得的修正。另 libsys4 #68 由 #69 取代，不應列作尚待完成的另一個獨立功能。

## 7. 尚未進官方的關鍵進度：Rufim 的 v14 實驗線

這是本次除官方差距之外，最值得採取行動的新發現。[Rufim 公開分支](https://github.com/Rufim/xsystem4/tree/new_system_versions)

| 分支 | 查證 HEAD／台灣提交時間 | 相對官方 master |
|---|---|---|
| `master` | `4bf3942`，8/9 16:18 | 自有 197、缺官方 39 |
| `new_system_versions` | `589cf2c`，8/27 05:51 | 自有 343、缺官方 39 |
| `tts-cheats` | 同一 `589cf2c` | 目前與上一分支指向同一提交，不能算兩份不同成果 |

最新實驗線的 343 筆獨有提交均在你的停工後，含 306 筆非 merge、37 筆 merge，作者為 Dmitry Kazantsev。**這是分歧程度，不是你「落後 343 個功能」。** 你的 WIP 與它則是各自獨有 23／351 筆，共同祖先同為 `e8bd5ab`；兩邊採取不同的 v14 實作路徑。

從作者的公開進度描述看，8/6 已提到標題選單、ADV 對話、backlog 與等待標記，之後還有 Dohna 地圖快照與 resume 工作；這比你停在開場 TV 的紀錄更深入，**但目前只是作者的場景進度聲稱與對應程式碼，未證明相同中文版本或這台 Mac 能到達同一位置**。[標題選單提交](https://github.com/Rufim/xsystem4/commit/9791a78faa)、[ADV／backlog 提交](https://github.com/Rufim/xsystem4/commit/8df8ca40d5)、[地圖快照提交](https://github.com/Rufim/xsystem4/commit/f45ae3e293)

值得定點比較的內容包括：

| 實驗線內容 | 公開程式／提交證據 | 與你的未完成項目的關係 |
|---|---|---|
| v14 物件、lambda／delegate、wrap／interface、Array 的 ownership／slot 契約 | 大量 VM／heap／FFI／Array 修改 | 適合用來對照你的 2-slot 與 double-free；不同物件模型不適合零散搬行 |
| Parts activity、parent、layout、文字／輸入路徑 | 8 月多批 PartsEngine 實作 | 與標題缺元素、對話與 Parts 互動相關，但不是已驗證可直接解你的問題 |
| v14 resume 與 heap generations | [13a2f5f](https://github.com/Rufim/xsystem4/commit/13a2f5f) | 有處理恢復後失效 delegate、slot 世代、heap holes；與存讀／lifetime 高度相關 |
| 存檔 interface arrays／bound checks／corrupt-save 處理 | 8/16 多筆 savedata／FFI 提交 | 適合在你的 Save→Load gate 比較格式與行為 |
| 真正 APEG video decoder 與 Parts movie | [d2f1f4c](https://github.com/Rufim/xsystem4/commit/d2f1f4c)；新增 `src/apeg.c`、`src/movie_apeg.c`、工具與介面 | 比你的 SOND/Ogg 音訊 fallback 更深入；值得另開解碼器驗證，不代表 TV 白色一定需它修 |
| resume garbage／leak diagnostics、字體、剪裁與控制元件 | [2685dda](https://github.com/Rufim/xsystem4/commit/2685dda)、8/26 後續提交 | 可作長跑、畫面差異和效能診斷的參考 |

APEG 提交聲稱與 Python 參考在七個遊戲逐位元組比對；本次只確認程式碼和提交中的陳述，**未重現這項測試，也未取得其外部分析工程的完整驗證資料**。其相容性表仍沿用舊狀態，不能作為 Dohna 已通關或 macOS／中文已驗證的證明。

它還使用自己的 `Rufim/libsys4`，最新 pin `703493c`。若要比較，必須連同它的相依套件、編碼、存檔格式與執行設定一起建立獨立樣本；直接混用你的 libsys4 不具可比性。此次未對第三方分支進行建置或遊戲測試，也未全面審查其 libsys4。

抽查還發現三項實際限制：其 `src`／`include`／README 搜尋未見 `GB18030`／`ain_is_gb18030` 對應路徑，不能假設能直接取代你的中文移植；Delegate 的 `Equals`／`ToString` 仍為 TODO；`System_Error` 預設記錄後繼續執行，因此驗收要檢查 game assert，不能只看程序仍存活。[Delegate 實作與 TODO](https://github.com/Rufim/xsystem4/blob/589cf2c7599761e30fc7b9a48ef6d8e106ef76df/src/hll/Delegate.c)、[System_Error 行為](https://github.com/Rufim/xsystem4/blob/589cf2c7599761e30fc7b9a48ef6d8e106ef76df/src/hll/System.c#L90)

## 8. 合併風險：已做靜態試驗

把最新 WIP 與官方 `fa09b00` 做三方 merge-tree，確認有 **4 個程式文字衝突**，另有 **1 個子模組指標衝突**：

| 檔案／區域 | 原因與處理方向 |
|---|---|
| `src/audio_mixer.c` | 官方 BGM loop／metadata 更新與你的音訊資源類型修改交會；要保留 ASSET_VOICE 路徑 |
| `src/parts/motion.c` | 官方 skip speedup 修改進入同一區域；此文字衝突部分源於局部排版，不能誇大為已證明的算法衝突 |
| `src/parts/parts_internal.h` | 官方字形／文字結構更新與你的 GB18030 字元緩衝交會；四位元組字元＋NUL 需要保留 `ch[5]` 能力 |
| `src/parts/text.c` | 字型快取／字寬更新交會中文解碼；保留多位元組解碼，再納入新的排版行為 |
| `subprojects/libsys4` | 兩方 pin 不同，需要先整合子庫再 pin 到經驗證的結果 |

另外，在獨立 libsys4 clone 對 `8c93946` 與 `cbc57ce` 做 merge-tree，**實際確認 `src/instructions.c` 有內容衝突**；這比只看到父庫 gitlink 衝突更具體。應保留 v14 正確的指令寬度與既有測試，再對接官方 initialization／delegate metadata。

這些是靜態合併結果，沒有完成衝突解決、建置或執行驗證。「只有四個檔案衝突」不代表只有四個風險：存檔格式、resource IDs、VM ownership 與語音資源分類可能自動合併但語義不合。

Rufim 最新線與你的檔案樹差異更大，為 130 檔、+41,549／−18,432 行；另一次 merge-tree 查得 **36 個文字／add-add 衝突檔案，加 1 個子模組衝突**，涉及 VM、heap、page、FFI、Array、Delegate、PartsEngine、resume 與 savedata。它適合先做能力比較與設計借鑑，暫不建議整支作預設合併目標。

三條線的 PartsEngine `CURRENT_SAVE_VERSION` 分別是：你的 **3**、官方 **5**、Rufim **14**。這是 Parts 存檔 schema 的版號，不是 AIN v14 支援程度。它們的舊檔可讀性、往返與相容性必須另驗，不能共用正式存檔目錄試跑。

## 9. Releases、PR 與仍未解決的事項

- 官方最新正式 release 仍為 [1.0.0](https://github.com/nunuhara/xsystem4/releases/tag/1.0.0)，發布於台灣 2025/9/29 11:00；這兩個月的新增主要在 master／nightly。
- [nightly](https://github.com/nunuhara/xsystem4/releases/tag/nightly) 是重複更新的 release：原始 release 日期在 2025/11，不代表 binaries 過期；此次 asset 更新到台灣 2026/8/26，提供 Windows／Linux 檔案，未見 macOS asset。因此你的 macOS 發行與驗證仍需要自己的建置流程。
- [最新官方 Build workflow](https://github.com/nunuhara/xsystem4/actions/runs/32920457751) 在台灣 8/26 成功，對應 `fa09b00`；它只證明該 workflow 通過，不能代替你的 macOS／中文／v14 實測。
- 即時 API 查得官方 **0 個 open PR、7 個 open issues**。停工後下表 PR 已合併；不能依快取頁面仍顯示 open 就算成未完成。
- 你的 [#297：v14.1 calling convention／Dohna macOS](https://github.com/nunuhara/xsystem4/issues/297) 仍 open，目前查到的留言皆來自你自己，沒有其他人的公開回覆；未見能據此認定官方已接手完成移植的證據。
- [#374：AetherKiri 整合詢問](https://github.com/nunuhara/xsystem4/issues/374) 仍 open，是整合需求，不是已交付功能。
- 新近問題 [#359 數字鍵盤](https://github.com/nunuhara/xsystem4/issues/359)、[#370 閃光效果 regression](https://github.com/nunuhara/xsystem4/issues/370) 已 closed，對應輸入修正及 libsys4 AFA 後續調整；[#356 construction framebuffer](https://github.com/nunuhara/xsystem4/issues/356) 也已修正關閉。
- 其餘 open issues 為 #308、#295、#286、#252、#178，主要是 emulator／既有遊戲相容性或顯示問題。issue 數量不能直接等同你的待辦數量。

## 10. 下一步：以可驗收成果安排復工

**建議採「保留最新 WIP → 建立可見畫面／對話基準 → 分批補官方 → 用 Rufim 解法作定點比較」的方向。** 你的自有 v14／中文工作仍有保留價值；同時第三方現在已出現相當多重疊工作，值得在繼續長期獨立逆向前比較。

以下是建議工作單，尚未執行；時間順序依賴驗收，不對逆向問題承諾固定工期。

| 順序 | 要交付的成果 | 通過條件 |
|---|---|---|
| 0 | 固定復工基準 | 記錄 WIP `484f4bc`、libsys4 `8c93946`、工具鏈、build 身份；保存既有 logs／Wine 基準與存檔副本；沿用現有測試隔離工具 |
| 1 | **macOS 實際視窗顯示** | 調查 `gfx_swap → SDL_GL_SwapWindow`；同一時刻比對桌面畫面與 framebuffer，標題／開場有一致可見內容。present chain 是調查方向，精確根因仍待證實 |
| 2 | **可控的中文對話** | 第一頁不操作至少 20 秒不自行前進；一次點擊只進一頁；名牌、正文、NEXT、對話框可見；以 Wine 原版同場景比較 |
| 3 | **消除 VM_PAGE 生命週期錯誤／重複釋放** | 重新建置同一 SHA 的 ASan／UBSan；走 NewGame→ADV→TV→下一場景，沒有 VM_PAGE double-free／越界；不能只驗標題 100 秒。歷史 log 證明重複釋放警告，未證明該次因此崩潰 |
| 4 | 小批官方正確性修正 | 優先測試 FFmpeg `nb_samples`、DAP、numpad、construction guard；再配套整合 libsys4，解指令 metadata 衝突並跑既有測試 |
| 5 | 存讀與共用 Parts 改善 | 分批納入 save format／CG by name／多行文字／排版／音訊 loop；保留 GB18030、voice 與 v14 契約；每批都有前後比較 |
| 6 | Rufim 有限範圍 A/B 比較 | 使用其完整相依 pin，與你的 WIP 在相同合法遊戲資料、隔離存檔下比較：可見畫面、等待、APEG、Save→Load；判斷借鑑哪些模組或是否值得換底 |
| 7 | 可玩 beta 的門檻 | 存→退出→讀成功，backlog／語音／設定正常，TV 後核心遊戲迴圈可達，至少 30 分鐘互動與記憶體觀測通過；再補廣泛特效／跨平台／打包 |

Rufim 的**靜態分析可與步驟 1–3 並行**；它的獨立執行比較可以提早進行，但在有相同場景、輸入與畫面基準之前，不宜宣布它比較穩定或更接近可玩。若它在相同中文 macOS 場景已通過上述關鍵條件，再評估將你的中文／macOS 修正移植到較成熟的那一線；若未通過，則選取能用測試證明的局部解法。

近期最有價值的第一個交付，應是 **「可見視窗正常，中文第一段對話能停住並逐句點擊」的可重現版本**。這會建立之後更新、借用第三方實作與修記憶體問題都能共用的判準。

## 11. 附錄與證據

以下附錄列全量官方缺失提交、libsys4 缺失提交、外部 fork 活動與本機來源。另有機器可讀證據 JSON 及 Rufim 停工後提交目錄，供後續工程比對。

### A. 官方 xsystem4 尚未納入的完整 47 筆提交

日期為首次納入官方 master 的台灣時間；非 merge 包括程式、文件與子模組 pin 更新，不能全當作獨立功能。

| 首次納入 master | 提交 | 類型 | 相對停工 | 作者 | 原始主旨 |
|---|---|---|---|---|---|
| 2026-07-07 09:24 | [74461f5](https://github.com/nunuhara/xsystem4/commit/74461f5b9c6166cc7b9115d9bab536a31c7f21e7) | 合併 | 停工前已缺 | Nunuhara Cabbage | Merge pull request #357 from kichikuou/issue-356 |
| 2026-07-07 09:24 | [d8479ad](https://github.com/nunuhara/xsystem4/commit/d8479ad80aa14601bd1ed2c2c0e6ff9436a2dfc3) | 非合併 | 停工前已缺 | kichikuou | parts: Fail construction build without target texture |
| 2026-07-14 08:55 | [c0cdf4f](https://github.com/nunuhara/xsystem4/commit/c0cdf4f0700fb044e32b182b00ad5610b152b40d) | 非合併 | 停工後 | kichikuou | debugger: Add "3d" debugger commands |
| 2026-07-14 08:55 | [e260b82](https://github.com/nunuhara/xsystem4/commit/e260b82828e3ed9a0c3b4107f7cea6fe3d33be6b) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #358 from kichikuou/3d-debug |
| 2026-07-17 12:03 | [6d05546](https://github.com/nunuhara/xsystem4/commit/6d05546c2c995afaf37e5078f6e065f3c38f9235) | 非合併 | 停工後 | kichikuou | input: Handle numpad keys with NumLock disabled |
| 2026-07-17 12:03 | [d63e71e](https://github.com/nunuhara/xsystem4/commit/d63e71e85d364227b3fe477ce5fca56c53ad85f5) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #360 from kichikuou/numpad |
| 2026-07-20 12:21 | [ca22543](https://github.com/nunuhara/xsystem4/commit/ca2254360cd99a21e5a3350ff0ad02ef323484fa) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #361 from kichikuou/libdeflate |
| 2026-07-20 12:21 | [81f9113](https://github.com/nunuhara/xsystem4/commit/81f9113906cb97b50c86a099c921a51733ffe9ef) | 非合併 | 停工後 | kichikuou | Use libsys4 wrappers for zlib operations |
| 2026-07-25 23:50 | [d17fc30](https://github.com/nunuhara/xsystem4/commit/d17fc3073169540ab4d96d2f311f6be94d3fdd55) | 非合併 | 停工後 | kichikuou | pl_mpeg: Support MPEG-2 program stream headers |
| 2026-07-25 23:50 | [b3bcd79](https://github.com/nunuhara/xsystem4/commit/b3bcd79baef983642db2ebca2dbc1e5f9719dba6) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #362 from kichikuou/pl_mpeg |
| 2026-07-25 23:50 | [f0c4db7](https://github.com/nunuhara/xsystem4/commit/f0c4db718d4b10f04292c5d6d7a3d5844388f894) | 非合併 | 停工後 | kichikuou | movie_ffmpeg: Use nb_samples for audio frame length |
| 2026-07-25 23:50 | [a38c556](https://github.com/nunuhara/xsystem4/commit/a38c55602d3f936fbc65b7142e6a4afa47366520) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #363 from kichikuou/ffmpeg |
| 2026-07-27 00:13 | [d5b27a0](https://github.com/nunuhara/xsystem4/commit/d5b27a0a47d0dea7361ffbf37bb59df8ad9ad730) | 非合併 | 停工後 | kichikuou | KiwiSoundEngine: Fix BGM loop for Rance 9 |
| 2026-07-27 00:13 | [433e3be](https://github.com/nunuhara/xsystem4/commit/433e3be3e6c208159d203c97385ba6bf8bf76a5b) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #364 from kichikuou/rance9-bgi |
| 2026-08-01 21:25 | [b68e162](https://github.com/nunuhara/xsystem4/commit/b68e1622fd4ef3e70f053945cc82850b19057f76) | 非合併 | 停工後 | kichikuou | 3d: Implement DrawShadow mesh attribute |
| 2026-08-01 21:25 | [09a0e33](https://github.com/nunuhara/xsystem4/commit/09a0e3329aa4b406b743477c6ee18f509ece8489) | 非合併 | 停工後 | kichikuou | 3d: Disable specular with zero power |
| 2026-08-01 21:25 | [f146320](https://github.com/nunuhara/xsystem4/commit/f146320e36078b8226b3cf9c870e1def6d640459) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #365 from kichikuou/3d |
| 2026-08-03 02:12 | [a2524ba](https://github.com/nunuhara/xsystem4/commit/a2524bafd5861162740cb293cae654fb1eadddd4) | 非合併 | 停工後 | kichikuou | PartsEngine: Stabilize save format for Rance 9 |
| 2026-08-03 02:12 | [8e339d1](https://github.com/nunuhara/xsystem4/commit/8e339d153765db66f700207a4ff517b31ad0837b) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #366 from kichikuou/save |
| 2026-08-05 09:50 | [d65a1e6](https://github.com/nunuhara/xsystem4/commit/d65a1e6d9b702e55b792b9fb13cdc42c4141264b) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #367 from kichikuou/rance9 |
| 2026-08-05 09:50 | [68e8257](https://github.com/nunuhara/xsystem4/commit/68e8257c2759bed5876ae0d2a29d59e0f802daad) | 非合併 | 停工後 | kichikuou | game_compatibility.md: Add Rance 9 (JA) as supported |
| 2026-08-06 09:42 | [4e0dd82](https://github.com/nunuhara/xsystem4/commit/4e0dd82776e4297596d3c137d7b2dc75bb4f8261) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #368 from kichikuou/libsys4 |
| 2026-08-06 09:42 | [77d9e77](https://github.com/nunuhara/xsystem4/commit/77d9e77053876e1de123a3eea2f7d85d17809109) | 非合併 | 停工後 | kichikuou | Update libsys4 |
| 2026-08-09 01:10 | [e699fb0](https://github.com/nunuhara/xsystem4/commit/e699fb0aa2fec16f0e79bdb65d068a73bc2dad60) | 非合併 | 停工後 | Nunuhara Cabbage | DAP: ensure messages are null-terminated |
| 2026-08-09 23:32 | [a7e973c](https://github.com/nunuhara/xsystem4/commit/a7e973c0a9091c4c84cdfccb697406b6a6c19901) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #369 from kichikuou/rance9-v2 |
| 2026-08-09 23:32 | [4dc9792](https://github.com/nunuhara/xsystem4/commit/4dc97926320c0f09add7000f672e3b261fa262a9) | 合併 | 停工後 | Nunuhara Cabbage | Merge branch 'master' of github.com:nunuhara/xsystem4 |
| 2026-08-09 23:32 | [33f1295](https://github.com/nunuhara/xsystem4/commit/33f1295237d6fc4b368d166d26ee34596cee2f0c) | 非合併 | 停工後 | kichikuou | Resolve archive paths in asset_manager_load_archive |
| 2026-08-10 10:00 | [5bb36c7](https://github.com/nunuhara/xsystem4/commit/5bb36c75243f655126f971dbeb99d3ac602612cc) | 非合併 | 停工後 | Nunuhara Cabbage | Update libsys4 |
| 2026-08-12 10:13 | [62d38aa](https://github.com/nunuhara/xsystem4/commit/62d38aaf590447b151e1c1d8c3a0775f83101b68) | 非合併 | 停工後 | kichikuou | parts: Reference construction process CGs by name |
| 2026-08-12 10:13 | [ebc3199](https://github.com/nunuhara/xsystem4/commit/ebc319994eb48412fe0c5f70b08ba90e1126c638) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #371 from kichikuou/rance9-v2 |
| 2026-08-12 10:14 | [9bb871d](https://github.com/nunuhara/xsystem4/commit/9bb871d9fd53e37f03349ac51d1926c10d0911b4) | 非合併 | 停工後 | kichikuou | parts: Fix loss of line breaks in saved multi-line text |
| 2026-08-12 10:14 | [b1f8948](https://github.com/nunuhara/xsystem4/commit/b1f8948b743cb34eb3736d4f0552c4d5449e538b) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #372 from kichikuou/parts-load-text |
| 2026-08-14 09:28 | [0d4eb2a](https://github.com/nunuhara/xsystem4/commit/0d4eb2a2a7720734599af405cdff60e74e0acb8c) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #373 from kichikuou/math-rand |
| 2026-08-14 09:28 | [d800e12](https://github.com/nunuhara/xsystem4/commit/d800e12f597aaf57651fc4a7f5dde2f0399e23cf) | 非合併 | 停工後 | kichikuou | Math: Replace rand() with mt19937 |
| 2026-08-14 11:14 | [4e20a49](https://github.com/nunuhara/xsystem4/commit/4e20a499d4a123a4746801ad1f18b3ae0033dbe9) | 非合併 | 停工後 | Nunuhara Cabbage | parts: use ceilf(ch->advance) for text layout |
| 2026-08-15 01:48 | [5cbc697](https://github.com/nunuhara/xsystem4/commit/5cbc697f234011070bcba2b199348686fd21ed16) | 非合併 | 停工後 | Nunuhara Cabbage | parts: use bold width for text layout |
| 2026-08-15 02:30 | [a19c200](https://github.com/nunuhara/xsystem4/commit/a19c2008705bfda4891d58e95b874e8ec5e1aa3e) | 非合併 | 停工後 | Nunuhara Cabbage | parts: clean up text layout logic |
| 2026-08-15 03:40 | [e3bf653](https://github.com/nunuhara/xsystem4/commit/e3bf653165432c4476b4954d7098122ddef1dab9) | 非合併 | 停工後 | Nunuhara Cabbage | Fix crash in Shaman's Sanctuary |
| 2026-08-15 06:17 | [5d032d7](https://github.com/nunuhara/xsystem4/commit/5d032d739e3a693ff000741a837d7c4061e02d1f) | 非合併 | 停工後 | Nunuhara Cabbage | Fix various memory leaks |
| 2026-08-15 08:26 | [5fb3c49](https://github.com/nunuhara/xsystem4/commit/5fb3c49f757ac992fb02316f58e5ce5b39eed5a9) | 非合併 | 停工後 | Nunuhara Cabbage | parts: clear cached font when rerendering text |
| 2026-08-15 12:51 | [e76762d](https://github.com/nunuhara/xsystem4/commit/e76762d11d153d5d980034f48dcbe3f8b529854f) | 非合併 | 停工後 | Nunuhara Cabbage | Implement EFFECT_DOWN_UP_CROSSFADE |
| 2026-08-17 02:05 | [05820dc](https://github.com/nunuhara/xsystem4/commit/05820dc5367794f3b06f5cf71efab0ca31f982ec) | 非合併 | 停工後 | Nunuhara Cabbage | parts: RemoveController returns delegate indices |
| 2026-08-21 10:23 | [135cae2](https://github.com/nunuhara/xsystem4/commit/135cae216f0cbeb9589c301f3c867809b28cafe1) | 非合併 | 停工後 | Nunuhara Cabbage | Mitigate UI performance issue in Rance 9 |
| 2026-08-24 01:11 | [bf3430d](https://github.com/nunuhara/xsystem4/commit/bf3430dbad79c8fe84ec10936190559f97f5540c) | 非合併 | 停工後 | Nunuhara Cabbage | Improve EFFECT_VWAVE_CROSSFADE |
| 2026-08-24 01:39 | [b30a107](https://github.com/nunuhara/xsystem4/commit/b30a107fda352003e7b4ff395015f364b5915d98) | 非合併 | 停工後 | Nunuhara Cabbage | Implement EFFECT_VWAVE_SCROLL_CROSSFADE |
| 2026-08-25 08:44 | [4233c7d](https://github.com/nunuhara/xsystem4/commit/4233c7d294f57a64ab02b7da90923b9499e5b6ac) | 非合併 | 停工後 | Nunuhara Cabbage | Fix EFFECT_CROSSFADE_MOSAIC |
| 2026-08-26 09:48 | [fa09b00](https://github.com/nunuhara/xsystem4/commit/fa09b0059f3ffcc7b9e853f330e832ea75d74502) | 非合併 | 停工後 | Nunuhara Cabbage | Implement PartsEngine.SetSpeedupRateByMessageSkip |

### B. libsys4 尚未納入的完整 20 筆提交

| 首次納入 master | 提交 | 類型 | 相對停工 | 作者 | 原始主旨 |
|---|---|---|---|---|---|
| 2026-05-20 11:41 | [1ee0845](https://github.com/nunuhara/libsys4/commit/1ee08454cc728b2df41e06578d21124ba3c3081c) | 非合併 | 停工前已缺 | kichikuou | flat: rename uk_int to generate_mipmap in CG library struct |
| 2026-05-20 11:41 | [649c913](https://github.com/nunuhara/libsys4/commit/649c913c8ba3d59d022454477ee40ad961cdc08d) | 非合併 | 停工前已缺 | kichikuou | flat: fix cg.size and payload_off for version>0 CG entries |
| 2026-05-20 11:41 | [df1c2a6](https://github.com/nunuhara/libsys4/commit/df1c2a62b9c0072e5de8967bf08ab08b3e9071b1) | 合併 | 停工前已缺 | Nunuhara Cabbage | Merge pull request #62 from kichikuou/flat-cg |
| 2026-07-06 02:22 | [22009d4](https://github.com/nunuhara/libsys4/commit/22009d4e68e3a057532abd97dc229826bb46aa2a) | 非合併 | 停工前已缺 | kichikuou | flat: Add binary writers |
| 2026-07-06 02:22 | [009c235](https://github.com/nunuhara/libsys4/commit/009c235e158c24ce905f755124b54b0776d27706) | 合併 | 停工前已缺 | Nunuhara Cabbage | Merge pull request #63 from kichikuou/flat |
| 2026-07-07 11:55 | [5964272](https://github.com/nunuhara/libsys4/commit/5964272c8783f151fa0e112e63c07fd867826617) | 合併 | 停工前已缺 | Nunuhara Cabbage | Merge pull request #64 from kichikuou/flat |
| 2026-07-07 11:55 | [76c1643](https://github.com/nunuhara/libsys4/commit/76c164312ebf95560e20964cdc7295522c890726) | 非合併 | 停工前已缺 | kichikuou | Expose flat_free_library |
| 2026-07-14 08:57 | [67b740f](https://github.com/nunuhara/libsys4/commit/67b740f0a21a97d2437e1f7b0ad387389ce4a278) | 非合併 | 停工後 | kichikuou | instructions: Update delegate instruction metadata |
| 2026-07-14 08:57 | [05a4d58](https://github.com/nunuhara/libsys4/commit/05a4d58412b1df05c06ba156e7a2290de040a857) | 非合併 | 停工後 | kichikuou | ain: Initialize instruction metadata for ain version |
| 2026-07-14 08:57 | [fe20525](https://github.com/nunuhara/libsys4/commit/fe2052597e6b533bb8668530f8792aa7034f20c8) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #65 from kichikuou/instructions |
| 2026-07-20 11:14 | [625b0b8](https://github.com/nunuhara/libsys4/commit/625b0b84bf5e8c4f11ac754b1d97c53e84a3664f) | 非合併 | 停工後 | kichikuou | Add optional libdeflate backend for zlib streams |
| 2026-07-20 11:14 | [bbabc8b](https://github.com/nunuhara/libsys4/commit/bbabc8b10f4d6950075a7b54e5328f5828a73189) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #67 from kichikuou/libdeflate |
| 2026-07-20 11:21 | [a8df981](https://github.com/nunuhara/libsys4/commit/a8df98198e1a16fe2219683cf3ded72afe8338fd) | 非合併 | 停工後 | Nunuhara Cabbage | ajp: fix invalid buffer read when mask is absent |
| 2026-07-25 23:47 | [43472ac](https://github.com/nunuhara/libsys4/commit/43472ac51ff89c0c1d54ea3f77bac7533d569cc3) | 非合併 | 停工後 | Dmitry Kazantsev | afa: fall back to sequential indices when file IDs are not unique |
| 2026-07-25 23:47 | [d3aea05](https://github.com/nunuhara/libsys4/commit/d3aea059331e33546dceeb57318249a13ba287cb) | 非合併 | 停工後 | Dmitry Kazantsev | afa: only fall back to sequential indices when all IDs are identical |
| 2026-07-25 23:47 | [d0c52d5](https://github.com/nunuhara/libsys4/commit/d0c52d58b488c97bdf47c578088e41653b892f63) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #66 from Rufim/fix-afa-duplicate-ids |
| 2026-07-25 23:48 | [a4867ff](https://github.com/nunuhara/libsys4/commit/a4867ff410a644af430891fd554cafb94ab0aef1) | 非合併 | 停工後 | kichikuou | QNT: Accept ALDExplorer-encoded alpha data with odd heights |
| 2026-07-25 23:48 | [b853346](https://github.com/nunuhara/libsys4/commit/b853346083e2509eec531adfe957ec600dfd8692) | 非合併 | 停工後 | kichikuou | QNT: Remove unnecessary image buffer padding |
| 2026-07-25 23:48 | [39eec1a](https://github.com/nunuhara/libsys4/commit/39eec1a8065903cda06ac42b0547cd93daabdaa3) | 合併 | 停工後 | Nunuhara Cabbage | Merge pull request #69 from kichikuou/qnt |
| 2026-08-10 09:54 | [cbc57ce](https://github.com/nunuhara/libsys4/commit/cbc57ce5642dadbbeda3c4894e1c2da7e15dbbff) | 非合併 | 停工後 | Nunuhara Cabbage | Partially revert #66 |

### C. 全部外部 11 個公開 fork 的活動

下表是 repo 任一分支最後 pushed_at，可能只是同步，不等於功能完成時間。未在停工後推送的 fork 不列作這段期間的新成果。kichikuou 的功能工作應由已合併 PR 理解，不能只看他的 fork master。

| 公開 fork | 最後推送（台灣） | 停工後活動判讀 |
|---|---|---|
| [Rufim/xsystem4](https://github.com/Rufim/xsystem4) | 2026-08-27 19:26 | 已深入查證 v14 實驗線；見正文與提交目錄 |
| [kvokkka/xsystem4](https://github.com/kvokkka/xsystem4) | 2026-07-07 05:35 | 未見停工後推送 |
| [orbisai0security/xsystem4](https://github.com/orbisai0security/xsystem4) | 2026-05-26 10:35 | 未見停工後推送 |
| [liufengpm/xsystem4](https://github.com/liufengpm/xsystem4) | 2026-05-11 22:05 | 未見停工後推送 |
| [MasayoshiFujiwara/xsystem4](https://github.com/MasayoshiFujiwara/xsystem4) | 2025-10-22 11:52 | 未見停工後推送 |
| [brombrom-hani/xsystem4](https://github.com/brombrom-hani/xsystem4) | 2026-06-22 21:05 | 未見停工後推送 |
| [southdy/xsystem4](https://github.com/southdy/xsystem4) | 2025-04-09 10:16 | 未見停工後推送 |
| [frelixir/xsystem4](https://github.com/frelixir/xsystem4) | 2024-09-19 21:09 | 未見停工後推送 |
| [toufuguy/xsystem4](https://github.com/toufuguy/xsystem4) | 2023-01-03 21:52 | 未見停工後推送 |
| [nao1215/xsystem4](https://github.com/nao1215/xsystem4) | 2023-02-04 23:08 | 未見停工後推送 |
| [kichikuou/xsystem4](https://github.com/kichikuou/xsystem4) | 2026-08-14 09:29 | 已深入查證；主要進度對應 14 個官方已合併 PR |

### D. 本機證據索引

本機來源保留在原位置，本報告未搬移或改寫。這些路徑用於追溯原始證據；測試結果是歷史紀錄，未在本次重跑。

| 來源 | 精確位置／行號與用途 |
|---|---|
| 停工交接 | `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/worktrees/xsystem4-cn-on-upstream/PORT-STATUS-2026-07-09.md`；29–35 灰窗；36–47 等待／文字；48–51 未完項 |
| GUI 調查 | `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/logs/tests/gui-run-log.md`；57–60 Parent 修正；73–89 MSG／Array；90–96 最後 Motion 結論 |
| runtime trace | `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/logs/runtime/run-20260709-091121.log`；61–67 Join→Observer→EndWaitForClick；236–237 VM_PAGE 重複釋放警告 |
| 原版 Wine 行為基準 | `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/reports/visual-parity/wine-baseline-index.md`；24–34：TV 白畫面、NEXT、點擊等待驗收 |
| Wave 0 | `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/reports/wave-0/wave-0-report.md`；16–42：EXTF 假設已更正；44–50：歷史測試結果 |
| libsys4 一般測試 | `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/logs/tests/wave0-libsys4-testlog.txt`；33–34：2 PASS／0 FAIL；未複製原始環境資訊 |
| libsys4 sanitizer 測試 | `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/logs/tests/wave0-libsys4-asan-testlog.txt`；33–34：2 PASS／0 FAIL，適用於當時版本 |
| Wave 1 | `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/reports/wave-1/wave-1-report.md`；38–60：ABI 稽核／gate |
| Wave 2 | `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/reports/wave-2/wave-2-report.md`；15–31 舊測試迭代；42–45 未覆蓋範圍 |
| Wave 3 | `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/reports/wave-3/wave-3-report.md`；36–49：文字／資源 gate 与視覺限制 |
| 原計畫 | `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/plans/xsystem4-report-plan.json`；101、121、146、160：Wave 4–7；部分早期假設已過時 |
| 早期 briefing | `<USER_HOME>/Downloads/文件/文件_文字/xsystem4-briefing-v2.md`；mtime 2/2；35–63 為舊 VM bottleneck |
| 視窗調查入口 | `<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/worktrees/xsystem4-cn-on-upstream/src/video.c`；424 gfx_swap；466 SDL_GL_SwapWindow |

舊文件的 `xsystem4/builddir/src/xsystem4` 現場不存在。原 fork 實際 binary 是 `<USER_HOME>/xsystem4-dev/xsystem4/build/src/xsystem4`；最新 WIP 一般 binary 在上述協調目錄的 `builds/wave-1/build/src/xsystem4`。ASan binary 在 `builds/wave-2/asan-build/src/xsystem4`，mtime 早三天；復工應重新核對 build 與 SHA，不能只以檔案存在認定版本。

### E. 可重現的 Git 比較

本次分析 clone 位於本任務 `work/repo`，相依分析 clone 位於 `work/libsys4`。以下是已採用的唯讀比較；實際 merge-tree 只建立分析用 Git objects。

```bash
git rev-list --left-right --count origin/wip/post-checkpoint-2026-07-06...upstream/master
# 23 47
git merge-base origin/wip/post-checkpoint-2026-07-06 upstream/master
# e8bd5ab0362b0aacc9fb66a9050548f3e7c41b8c
git log --no-merges origin/wip/post-checkpoint-2026-07-06..upstream/master
git diff --stat origin/wip/post-checkpoint-2026-07-06...upstream/master
git merge-tree --write-tree origin/wip/post-checkpoint-2026-07-06 upstream/master
# 有衝突是本次查證結果，不是已完成合併
```

first-parent 歷史用來推算「何時首次進 master」；完整 SHA、原始 authored／committed 時間與分類已保存在同目錄的 `audit-evidence.json`。Rufim 全量提交另在 `commit-catalog.html`，可依来源、類型與關鍵字篩選。
