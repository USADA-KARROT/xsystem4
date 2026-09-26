# 2026-09-26 macOS／多娜多娜 CN 開發交接

**最新狀態：已保存第四階段的回呼引用修正，主畫面可出現，但新遊戲仍在人物 ID assertion 停止。尚非穩定可玩版，也未證實卡頓已完全解決。**

本 checkpoint 接續 2026-09-09 的 `7b4637d`，保存 9/20 原生路線研究、Array API 修正、9/24 生命週期修正、9/26 實機失敗與最新回呼修正。此处「第一至第四階段」指9/20之後的研究／修復批次，不代表原四項產品目標全部完成。

## 核心成果

| 批次 | 已完成 | 驗證限制 |
|---|---|---|
| 原生路線研究 | 原程式技術證據、可搜尋API對照地圖、研究報告及重跑工具 | 保留當時固定版本的分析，不代表原程式已完整重寫 |
| Array／FFI | 依實際宣告處理查詢callback、回傳與參數契約 | 局部測試不等同完整遊戲相容 |
| 生命週期 | callback／closure、page／array所有權、重入與heap重用相關修正 | 無畫面回歸通過項目不能代替GUI |
| 第四階段 | delegate_copy_argument對AIN_WRAP保留有效heap引用，避免活事件提前回收 | 涵蓋所有WRAP callback參數；新增事件測試完整清理仍失敗 |

最新原始事件bytecode測試：修正前第二次add就失效；修正後三次add及384次字串配置功能檢查通過，但清理仍有23個VM slots、exit87。其餘六種ASan/UBSan模式（Personality、observer、reentrancy、click、timer、heap-reuse）exit0。LeakSanitizer未啟用，不宣稱無洩漏。

最終optimized實機72.109秒：主畫面後按新遊戲，仍 `Personality.jaf:27 assert(id != "")`；本次double-free警告0行，之前同日optimized輪為10,627行。assertion之後仍有free-list異常，另有X_ASSIGN clamp。沒有ADV MSG。另一輪診斷確認人物ctor傳入值為-1，由WorkerCreator取得Take<string>結果第0項後傳入，根因尚未確定。

PERF為vsync=0時的present提交頻率，可能包含相同畫面，不能當作實際遊戲FPS。測試程序exit0也不代表成功；GUI runner遇到assertion回傳1並標記error_log。

## 主要文件

- [第四階段最新報告](outputs/第四階段-回呼參數修正報告-2026-09-26.md)
- [第四階段原始測試摘要](work/string-stage4-20260926/probe/final-results.json)
- [人物ID下一步入口](work/string-stage4-20260926/worker-followup.md)
- [第三階段實機結果](outputs/第三階段-實機測試結果-2026-09-26.md)
- [第二階段完成報告](outputs/第二階段-完成報告-2026-09-24.md)
- [第一階段Array修正](outputs/第一階段-Array介面修正-2026-09-20.md)
- [原生路線研究報告](outputs/xsystem4-原版逆向與原生路線研究-2026-09-20.md)
- [可搜尋API對照地圖（HTML，下載後用瀏覽器開啟）](outputs/原生研究-API地圖-2026-09-20.html)
- [重建及重跑說明](REPRODUCE.md)
- [公開附件與原始hash對照](publication-manifest.json)
- [上一個checkpoint](../2026-09-09/STATUS.md)

## 下一步

先用真Take<string>腳本與1–3個已知非空字串做有界所有權驗證：結果內容正確、釋放source後result仍有效、清理回到heap基準。必要時記錄Worker的X_REF→A_REF→NEW，判斷值在取出前或複製時變壞；若通過再回溯Concat與ID產生流程。不要繞過assertion或填假ID。其後處理23個slot殘留與實際遊玩幀時間。

## 保存範圍

來源檔直接提交於repo正式路徑，引擎提交為 `c3b5ff03024f3d043a33dd05c52656dfde70f996`。相較上次公開checkpoint新增7個來源檔的差異；21個累積修正檔均與最後測試工作副本逐byte相同，見[source-identity.json](source-identity.json)。libsys4固定於`8c939465910499b4802ec6dd619794ca58ba4708`，沒有未提交submodule修改。

`outputs/`保存交付成果；`work/`保存研究與fixture原始布局、必要baseline及執行紀錄。691份文字附件保留每份原始與公開衍生版SHA256。本機路徑去識別化；台詞省略。商業遊戲資產、原執行檔、存檔、截圖、完整AIN dump、編譯產物及帳號額度紀錄不公開。需要遊戲的測試必須自行提供合法原檔。

歷史報告中的「未推送」與額度敘述指當時狀態；本checkpoint完成保存後，以Git commit為發布身份。封存腳本中的個人路徑已替換為佔位字，不能不經環境設定直接執行。這次僅發布已完成的修正與證據，沒有開始下一階段除錯或重新宣稱遊戲通過。
