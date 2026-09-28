# 角色對白與陣列生命週期（2026-09-28）

本組修正 v14 陣列清空後遺失元素型別，以及淺層副本缺少獨立元素參照。正式修改只在 `src/hll/Array.c`，沒有調整繪製順序、訊息字色、遊戲 AIN 或 libsys4。

## 兩個已重現的缺陷

1. `Array.Free` 把 page 丟掉；`Clear` 換成整數空頁。訊息模型經過清空後，`EmplaceBack` 不再依原本的 CMessageText 宣告建立／回傳物件，ffi 會把整數頁的結果當兩槽參照。呼叫端期待一槽 struct，後續 At#1 讀不到有效訊息。
2. `ShallowCopy` 複製 struct／string 的 heap slot，卻未增加參照。釋放任一陣列就可能讓另一份的元素失效。訊息模型的 ClearMessageText 與角色集合的 GetAllInstances 都會用到 ShallowCopy；本組並未聲稱已完整重建原版所有 wrap 型別。

## 原版依據與宣告

- Free / Clear：dispatcher `0x644300`，跳表 `0x644f18`，index 6/7 共用 `0x64442e`；最終 `0x67ec50` 倒序釋放元素、清長度及 allocated flag，但保留 descriptor 與 stride。
- EmplaceBack：`0x6476e0` 依 retained descriptor 配置元素；STRUCT 初始化 `0x656a12`，struct/string wrap 回傳分支 `0x6477d3`。At / At#1 最終共用 `0x6499e0`。
- ShallowCopy / #1：index 4/5 分別經 `0x647610`／`0x647660` 到 `0x658d40`；原版產生 wrap 元素，struct/string 分支 `0x658e33`，setter `0x67f2e1` 對共享元素增加參照。

Free、Clear、EmplaceBack 都只有一種宣告形狀；At 與 ShallowCopy 各有兩份相同宣告。原型一致，本組沒有新增 ffi 特例。全 AIN 的直接 CALLHLL 分布如下，數字是靜態呼叫點，不是執行次數：

| API | tag 1 | tag 2 | tag 65538 | tag 65539 | 合計 |
|---|---:|---:|---:|---:|---:|
| Free | 16 | 93 | 57 | 11 | 177 |
| Clear | 3 | 5 | 7 | 1 | 16 |
| EmplaceBack | 1 | 18 | 0 | 0 | 19 |
| At#1 | 16 | 125 | 110 | 16 | 267 |
| ShallowCopy#1 | 0 | 60 | 0 | 0 | 60 |

At、ShallowCopy 第一份宣告皆無直接呼叫點。空頁查詢相容性涵蓋的 Count/Numof(predicate)、Find 各宣告統計，見 [AIN 路徑](ain-text-route.md)。[清空語義位址](native-array-semantics.md)與 [ShallowCopy/Where 位址](native-shallow-copy.md)皆來自原版 EXE 靜態反組譯，沒有執行原版。

## 實作範圍與獨立審查

- v14 的已知 ARRAY_PAGE 清空後保留 `a_type`、struct 編號與 rank，內容改為零長度；原本 NULL 及 pre-v14 行為保留。
- 審查者發現 NULL 改為 typed 空頁，可能讓 Find/Count 的型別或 callback 檢查從安全空值變成 VM_ERROR。已在 CountIf、FindValueRange、FindIfRange 先回傳空結果，並以真 ffi 檢查 callback 沒有執行、堆疊平衡及外部 owner 未減少。
- ShallowCopy 僅補 v14、rank 1、具體 struct/string（含 ref-array 別名）的共享 owner。移植內部仍用既有單槽 page 表示；primitive 的 owner+offset、巢狀陣列、多槽介面及完整 wrap descriptor 未重做，保留既有路徑。
- 元素 destructor 重入修改同一陣列、VM A_FREE、realloc 到零及其他清空入口不在本次已驗證範圍。沒有新增 VM_ERROR 或改動未知宣告綁定。

## 修正前後

| fixture | 6855c2f | 修正後 |
|---|---|---|
| dialogue-model | exit 86；清空後 EmplaceBack 回 2 槽、type14，無有效 struct | exit 0；反覆 Free/Clear 後仍為 struct259、一槽回傳，At#1 可讀合成文字 |
| dialogue-copy | exit 86；副本元素 refs 仍 1，應為 2 | exit 0；來源或副本先刪均可讀，refs 依序 1→2→1→0 |
| 完整驗證 | — | 36 模式 VERDICT PASS；deleted-event 為既有預期 exit87，其餘 exit0；sanitizer 0 |

另涵蓋型別與 rank 保留、外部元素 holder、typed int/string、空集合查詢、NULL、v13 相容；淺層複製檢查物件身分，沒有以深複製代替共享。

[清空 before](before-6855c2f.txt) · [副本 before](before-copy-6855c2f.txt) · [清空 after](after-dialogue-model.txt) · [副本 after](after-dialogue-copy.txt) · [全模式摘要](verify-summary.txt)。

## 畫面調查與被推翻的判斷

修正前必須區分 event 視窗 900028 與 main 900040：前者有非空文字，不代表後者正常。修正 typed clear 後，main 已有非空文字、glyph、正確螢幕位置。

調查曾把「文字剛畫完的圖」與「另一時點的空框」相比，推測後續部件蓋住正文。逐畫格驗證推翻此推測：同一畫格在 main 繪製完成、後續 sprite 完成與 scene 結束時均保留正文；同次執行的標準 framebuffer 也可見文字。空框截圖對應清除訊息／角色轉場期間。本組未因此修改 z 排序或圖層。

私有診斷只增加日誌與像素讀取，與正式引擎分開；所有遊戲截圖、原始日誌及 dump 留在 repo 外。正式引擎的 150 秒結果如下。

## 正式 GUI 結果

最終正式建置執行 150.223 秒，以時限結束，MSG 88、assertion 0、堆疊溢位 0。後段 heartbeat 到 `DohnaDohna@RunHome → Scene::RunResult<SceneAzito, GamePhase>`。已視讀標準 `xsys4_t18.png`：角色正文可見；基準同編號截圖的正文空白。截圖序號僅代表取樣時點，沒有宣稱兩張是完全相同的台詞畫格。

只修 typed clear 的中間版本曾於 115.575 秒出現 PlayerCollection 第 19/20 行斷言；補上已由原版和 fixture 確認的 ShallowCopy owner 後，完整 150 秒沒有再出現。這驗證了組合修正的結果；沒有對每次角色 ID 查找做完整追蹤，不能把所有角色集合問題都宣稱已解決。

最終峰值 RSS 為 1,898,332,160 bytes，記憶體成長仍未解決；不能由單次短測宣稱效能或洩漏已修好。存讀檔往返、據點所有互動、長時間遊玩、完整中文切字及逐字顯示效果仍未驗證。下一組應一併處理 GetStructPageList 與 DeserializeStruct 的持久化。

[GUI 量測與影像雜湊](gui-summary.json)。母片未修改、原版 EXE 未執行，libsys4 維持 `8c93946`。

## 重跑

先依 [harness](../../harness/README.md) 設定合法遊戲工作副本與 repo 外 XS4_WORK；不可執行母片。

```bash
H=docs/checkpoints/2026-09-28/harness
bash "$H/before-check.sh" 6855c2f dialogue-model dialogue-copy
bash "$H/verify-step.sh" dialogue-array-lifetime
bash "$H/gui-run.sh" dialogue-lifetime 150
```

before-check 要求 src/include 乾淨。原版位址可使用 `../achievement-text/pe_dis.py` 的位址模式唯讀重查；自行設定 XS4_EXE_DUMP，輸出仍留 repo 外。公開檔案不包含遊戲資產、執行檔、存檔、截圖或完整 dump。
