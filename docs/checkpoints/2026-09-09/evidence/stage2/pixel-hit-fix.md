# Flag-gated pixel hit testing — 2026-09-09

已確認並修復本輪標題「實體點新遊戲卻命中讀取」的原因：兩張透明斜條圖的矩形重疊，而現有 PixelDecide setter 是 no-op，pactex loader 也未載入遊戲指定的像素判定旗標。修正只作用於啟用該旗標的 parts，沒有固定按鈕 ID、畫面座標或 DPI 倍率。

## 資料與程式證據

- 以現有 libsys4 的 `afa_open` / `ex_read` 唯讀解析 game-workcopy 的 `dohnadohnaPact.afa`，SceneTitle.pactex 的 ButtonStart 座標為 `(0,67,z6)`，ButtonLoad 為 `(0,122,z7)`。兩者 `鼠標指針ピクセル判定 = 1`。實際資料是 GBK 中文欄位，hex `caf398cbd6b8e198a5d4a5afa5bba5ebc5d0b6a8`；不可僅使用日文 SJIS 欄位。
- 證據：`pixel-tests/pact-title.log:2,36,64,98`；工具 `pixel-tests/pact_probe.c`。這份 log 的內容是布局/資源技術屬性，不是遊戲執行結果。
- Rufim 對照：`work/stage1/rufim-source/src/parts/input.c:89–166` 有 pixel alpha 判定及 mask，`src/parts/parts.c:4229` 將 SetPartsPixelDecide 導向該旗標，`src/hll/PartsEngine.c:4434` 載入日文 layout flag。本修正保留其「非零 alpha 可點擊」語意，但按本機 CN key 讀取，且對可變 texture 不永久快取。

## 修改

以下路徑相對 `work/stage2/source`，行號為本次完成時位置：

- `src/parts/input.c:52–112`：未開 flag 或沒有有效 texture 保留矩形判定；開 flag 的影像依實際繪圖的 position、local Z rotation、scale、origin、水平/垂直 deform 逆換算 texel，並套用 surface_area。alpha=0 不攔截點擊。裁切使用整張圖的 origin，沒有將完整 alpha 圖硬拉伸至裁切框。
- 靜態 `PARTS_CG` 首次判定讀一次 alpha，後續重用；其他會原地更新的 texture 使用即時單點 alpha，避免 gauge 等圖像快取過期。
- `src/parts/parts_internal.h:126–132,432`：每狀態 CG mask 與每 parts 的 pixel flag。
- `src/parts/parts.c:173–175,666–678`：state free/reset 與 dimensions/CG replacement 清除 mask；`1988–1991` SetPartsPixelDecide 保存 flag。所有正常 `_parts_cg_set` 經由 parts_set_dims 失效快取，包括同大小 CG 替換。
- `src/hll/pe_v14_activity.c:148–152,226–232,320`：精確辨識 SJIS 日文、GBK 日文、這份 GBK 中文 key；缺席或 0 保持 false。`1027–1038` 將 Parts_IsPartsPixelDecide 的 v14 false stub 覆蓋為實際 flag getter，與 AIN f8372 / f14140 / f14141 的 bool getter 契約相符。
- 原本 root 的 click candidate trace 保留，增加 `pixel` 欄位。未修改 message sidecar、render、queue 或 controller 行為。

## 驗證

`python3 work/stage2/pixel-tests/run.py`：

| 案例 | Checks | Failures |
|---|---:|---:|
| 原 parts_hittest + 同一 fixture | 36 | 14 |
| 修後 production routines | 36 | 0 |
| 修後 ASan + UBSan | 36 | 0 |

fixture 直接 include production parts/input.c 和活動 flag helper，並連結 production parts.c 的 cache、dimensions、surface routines；僅用 SDL 的矩形計算，沒有初始化 SDL、GL、遊戲或 UI。GPU 像素讀取由固定 RGBA 資料代替。覆蓋精確三種 encoded keys、缺少/0/非 int 欄位、透明上層放行不透明下層、alpha=1、半開邊界、cache 重用/同大小替換/重複清理、父子座標、scale、flip、rotation、裁切、動態 texture 同 handle alpha 改變與讀取失敗/無 texture fallback。

Baseline 僅替換原 hit-test routine；flag parser 與 cache lifecycle helpers 在三案均使用修後版本，因此 14 failures 是原 hit-test 對這些行為的失敗，並非聲稱 baseline 執行過完整舊 loader。沒有執行完整 parts destruction 或保存/讀取 round-trip；state-free 清理入口另做 source inspection。詳見 `pixel-tests/results.json`、`after-sanitizer.log`。三個 production C 檔 syntax-only 通過。

Root 的實機 `runs/pixel-string-watch/engine.log:39–52` 又獨立驗證：

- t=31426ms，實體點擊 screen `(169,162)`，SDL event/polled `(169,130)`，override `(-1,-1)`；window/drawable/logical 全部 `1280×720`。
- 900017（Load）：pixel=1，hit=0，default_hit=0。
- 900016（NewGame）：pixel=1，hit=1，default_hit=1。
- `S2 click target=900016`；root 同輪可見畫面進入正確 TitleClose 放大轉場。

因此本次透明區搶點擊已同時具備資料、production fixture 與實體 UI 證據。它不證明後續 WaitForClick／中文顯示已修復，也不是先前所有灰畫面的通用解釋。

## 範圍與下一步

- 尚未增加 flag 的 save-format 持久化，未驗證完整保存/讀取；從 pactex 新建的活動會按資料再次設定。這次不改現有存檔格式。
- 此 alpha 判定使用 base texture；沒有新增 alpha-clipper 合成取樣、3D perspective、movie/flash 特有變換或雙線性 alpha 閾值模型。未啟用 flag 的幾何判定不變。
- 動態 texture 的逐點 GPU 讀取可能有同步成本；這次標題是靜態 CG，重複判定使用 cache。
- 下一輪繼續以同實體位置驗證標題新遊戲、讀取及其他斜條互不搶點；中文顯示/ADV 等待另外按 root runtime trace 判斷，不由此修正推論。

## 額外唯讀 message layout

已輸出 `work/stage2/message-layout.log`，包含 main/mainB/event/plot 的布局與 GBK key hex，沒有擴充 message setter。main 的 SYS_信息窗口 pos `(662,607)`、origin=5；TextArea `(130,62,940,181)`；text-origin=1；font type0 / size25 / white；bold0、edge0；character spacing=-1、line spacing=10（log:23–58）。mainB 同 area/font，但文字黑色。後續是否載入這些初值，應先看 root 的實際 text/render trace。
