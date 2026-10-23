# G5 審查與證據狀態

**2026-10-23：程式 `f693086` 已推送並通過三方雜湊核對；首輪片頭像素失敗保留，同 binary 僅重跑一次後嚴格 50/50 通過；證據與公開文件終審皆為 0 High／Medium／Low。** 本文件整理獨立審查的範圍與證據。編寫者的自查不算獨立程式、證據或公開衛生審查。

## 來源綁定

程式 `f69308620134ec09826c4aa15b938735763b7d41`，parent `d9d13e8e7e37f86d410c08c7b9b7bd9c0283a0cb`，libsys4 `247f54434ddf73ba6cd33b2ed0fd557675012ef7`。已唯讀核對此 commit 的 11 檔皆與凍結快照相同。11 檔凍結快照的 tracked patch SHA-256：

`2363b27f6b598344405d4d453301d0b8eb9fb8f55e64de31608f0cbc2171e8b4`

新 fixture 不在 tracked patch 中，以下完整檔案雜湊包含它；不能只用 tracked diff 宣稱覆蓋新檔。工作副本另有尚未發布的交接文件修改，不屬本次 11 檔程式／探針快照。

| 儲存庫相對檔名 | SHA-256 |
|---|---|
| `docs/checkpoints/2026-09-28/harness/env.sh` | `60457ec8078f27c689785b9eb9e64a9b0ba667b3e480eecb717680f16521959d` |
| `docs/checkpoints/2026-09-28/harness/probe/auto_input_fixture.inc` | `87f8e3892d426227b72ed7b2ee49d85a58912eab628b0f1e873911991e67da15` |
| `docs/checkpoints/2026-09-28/harness/probe/runtime_probe.c` | `22ee803b8ca46ddb7c94fc2eacf2c8fd723fac7babf1ef47396c0cf87f80d899` |
| `src/hll/PartsEngine.c` | `ce6034441b08df4c3089b1de6d00662c3de7c4f5cc1dc2e1600331c04a8ac017` |
| `src/hll/pe_v14_activity.c` | `37b740dea45df58ffe93912144b8a1dd95f41d1d52dec81b9643f10e0807e286` |
| `src/hll/pe_v14_message.c` | `e518d1595bbfd720d35d8124ee55793fd07f0fbc81bd72a20191af63dbb3749d` |
| `src/input.c` | `87abd0771cc4dc07ababe5cd2cdf06b4647120ee414b41136b04939553c30eaf` |
| `src/parts/input.c` | `b33f26fe7018f4c71bc35323c3b1ee34502b05ae695b158b7c8e55afb1922ee8` |
| `src/parts/parts_internal.h` | `cf6f648212c8e7538dfdc5d7fb907b0b2302be7f31a82c2f74cbf6c2ab8fc767` |
| `src/parts/save.c` | `dff21fb26c1b0f0a8729b5433f00b36665b693ae010a96de004d53a65ae6a250` |
| `docs/checkpoints/2026-09-28/harness/probe/wheel_events_fixture.inc` | `6c23d9799de008e54de95b97b7f4e7bc40fdaea284ad135c947aff8c62e80531` |

## 已有獨立審查

| 查核 | 已完成範圍 | 結論與界線 |
|---|---|---|
| owner 更正複審 | 原版建立／重掛鏈的 bytes 與靜態解碼、一般父層和內部 owner 的分離、WE6／WE13 oracle | 0 High／Medium／Low。排除錯誤視覺父繼承，沒有用額外父事件冒泡掩蓋問題；不代表複合內部模型已完成。 |
| 更正版完整程式審查 | 11 檔、原版 18 段靜態證據、AIN callback／Clear 時序、舊版本 gate、HLL 形狀與 PWHE | 0 High／Medium／Low。未執行遊戲，不含最終原版商店與提交後驗證。 |
| 提交前 native 證據覆核 | 原始九路線 log／metadata、230 份模式 log、六組 mutant、16 次跨 reader、50 張完整 RGB 片頭及局部環節選擇畫面 | 0 High／Medium／Low。自行重算數字並實看指定圖；不等於原版完全相同或整組放行。 |
| 設定頁視覺疑慮覆核 | 指定基準／新版畫面及按鈕區域 | 疑似變色經原始 PNG 與 RGB 重算排除；沒有因此修改程式。 |
| 提交前新增原版／畫面補審 | 原要求逐項、原版操作與影像來源、商店扣款畫面、修前／後選擇頁、捲軸淡出量測與 AIN 靜態、11 檔來源未變 | 0 High／Medium／Low，可進入 code commit、parent before-check 與提交後驗證；不是推送放行，也未重跑先前大型驗證或重新完整審 C 程式。 |
| 私人片頭工具 M1 複審 | t311 僅接受固定來源兩候選；拒絕自動第三張及同名／連結替代；其他 49 幀規則保留 | M1 已處理，新增 0 High／Medium／Low。作者 10 組合成測試；獨立執行候選函式核對 6 種 t311 形狀與其他 49 幀；不是最後整組發布覆核。 |
| 最終證據覆核 | 11 檔來源、提交前後四組 verify、parent before、九路線與片頭單次重跑、完整 RGB 重算、M1 關閉及畫面限制 | 0 High／Medium／Low，證據覆核通過；隨後公開文件終審通過、程式推送三方核對一致；文件推送核對另存同步紀錄。 |

不同審查的時間與涵蓋範圍不同。較早報告的 Wine pending 已由後續局部量測補上一部分，不能說全部仍未做；英文商店同底項與效果／持久化尚未驗證，亦不能因 0 finding 就算通過；它們不屬 G5 原定最低驗收的額外阻擋項。

## 可重算結果

- [verify.tsv](verify.tsv) 由提交前、後各兩份原始 summary 逐列產生，共 460 列。每階段均預設／GBK 各 115，退出碼均符合預期、sanitizer 0、最後 `VERDICT PASS`。提交前摘要 SHA-256 分別為 `b6ba95b511dea5eeeaee51704cf616251b0109930d7eefdf990f2309f1a30ac5`、`7a8a7b48bc4f457a937abf82b510f65ca7be5af206070ad9b30b5e7d8530c370`；提交後分別為 `962bfed2dae89aaa93d53b79eaff05037345768a0a23398c93b4bc441a64dc2e`、`60dcca6f79ee542b5a3407e795b5704a66886f7a0162a5cb7b05cea46d650d8f`。
- 提交前 header 是 `d9d13e8` 加引擎修改 hash `502f546d1c4d`，提交後是 `f693086` 與空引擎差異 hash `e3b0c44298fc`；這只表示引擎差異為空，不宣稱所有文件工作樹乾淨。
- [before.tsv](before.tsv) 採正式提交後對 parent 執行的最終 fixture：14/15 案語義失敗，WE12 舊版防守案通過，probe rc 1、sanitizer 0；不是建置或啟動失敗。原始 log SHA-256 為 `1ad4e6e0d65c25b147562e985424941b33d6cbd0bd3efeb1801c2cbda4bb104d`，來源還原 11 檔相符。
- [routes.tsv](routes.tsv) 依提交前／後原始 log、run／phase metadata 與基準重算，兩階段各 9 條加提交後片頭單次重跑，共 19 次執行均符合日誌／操作門檻。MSG 使用 keepends 保留原始換行；點擊位置序列相同、退出碼與錯誤 0、每次 cursor 前後不變。前／後 binary 分列，fightr 目標差分別 58／65，仍保留為成因未明的例外。提交後首輪片頭 760.382 秒、MSG 451、49 clicks，嚴格 RGB 48/50 未過；單次重跑 760.229 秒、同樣 MSG／clicks，嚴格 RGB 50/50 通過。表中 PASS 只指日誌／操作，不抹除首輪像素 FAIL。
- 提交後八條一般路線的並排樣本由主代理實看：normal150、config、garage 三對 RGB 0 差，其餘五對背景或戰鬥配置仍有差異；不宣稱全圖或全路線一致。
- 6 組 mutant 均成功編譯後由語義斷言失敗攔住，還原後 15/15 案通過。舊 `ignore-parent` 使用錯誤 oracle，不列入最終六組。
- 4 類合成 XPE 12、16 次新舊 reader 流程通過；新版未設旗標內容與舊版相同，移除 PWHE／清除旗標後恢復原內容，舊 reader 讀含 PWHE 或移除 PWHE 後重存亦相同。限預設字元設定，未推廣到真實存檔。
- 提交前片頭 50/50 完整 RGB 相同；t311 只認列實際相同的既定候選 `023900-B`。提交後以同一 binary、AIN、起始存檔 manifest、種子、輸入與排程時間僅重跑一次，第二輪 50/50 完整 RGB 相同，t311 對既定 `023866-I` 為 0 差。主代理分別開啟四個並排樣本；不是此輪 Wine 全片同步比較，也不證明首輪差異原因已查明。
- 首輪原 audit 的 49/50 把 t311 額外自動候選算入，不能使用；嚴格 48/50 報告保留。t349 後續對既存 `099933-B` 診斷為 RGB 0 差，但 t311 仍不符兩個候選，沒有改寫為首輪通過。新嚴格工具 hash `bc7d71c81db1ff938ffd53990c5d20047448fdf179db236cdc7377e0d1bf881a`；第二輪 summary hash `56f8c385b192d3788d0a015d66c37dbc776fb5cd2a24cabfb927efedc3fd415a`。M1 修正及獨立複審完成，原工具及歷史證據未改，詳 [README](README.md)。
- 原版物品、回顧／據點、環節選擇局部畫面已由主代理實看，量測與限制見 [README](README.md)。原版／移植版品項順序、字型行距、持鍵條件與事件聚合方式存在差異，沒有宣稱全圖等價。
- 移植版底項購買只確認足額人工 seed 下扣款 50,000→22,000、列停用與框關閉；原版同項、級別效果及重載持久化尚未驗證。
- 新增選擇頁修前／後 50 張中 39 張全圖相同，包含 t05–t40 全 36 張穩定區間；同輸入、種子與持鍵下沒有滾輪誤選。原始 MSG 保留換行重新核對相同，SHA-256 為 `41ebf0047b8b120ffdccafe1c8df68acdea922f7bc0349f535dd21342fc7ba49`，不採早期去換行再接 LF 的 hash 作為 raw hash。
- 新增捲軸淡出量測支持出現、保持約 2 秒、之後淡出的行為；AIN 靜態支持 highlight timer 與 motion 設定。500 ms 圖片取樣未量得精確淡入／淡出曲線，也沒有 Wine 同步曲線比較。

## 已處理的證據解讀風險

| 風險 | 文件處理 |
|---|---|
| 把首次 Get 快照寫成原版雙 buffer | 明列為 SDL 適配；原版 Get 是純讀。 |
| 把內部 owner 和 visual parent 混為一談 | 保留第一版誤判、建立鏈反證、更正與仍未重建的內部模型。 |
| 把 root fallback 當成旗標已保存 | WE13 另核對 Load 後立即重存仍包含 root 自身旗標。 |
| 把舊 oracle 的 before／mutation 沿用 | 最終 before 已用正式提交的 fixture 對 parent 重跑；mutation 以新版六組為準。 |
| 把物品 ROI 相同寫成跨語系全圖相同 | 明列各自圖示追蹤、品項順序差異及時間／持鍵限制。 |
| 把回顧像素差寫成倍率差 | 行距分別為 34／37 px，單格各為 3 行；雙格不冒用不同累積量的畫面。 |
| 把環節選擇當作標準 ADV 分歧 | 分列；CMenuView 入口與實機未驗，不宣稱不可達，也不把它加成原文未要求的無條件關卡。 |
| 把九路線通過寫成 wheel consumer 全覆蓋 | 明列既定九路線沒有 wheel 排程，另看本組示範及原版局部證據。 |
| 把無當機等同功能完整 | 商店效果、持久化、全片音訊與複合元件範圍各自保留未驗證。 |
| 把額外自動片頭候選算入 t311 通過 | 原 audit 的 49/50 不作放行證據；M1 修正限固定兩候選且獨立複審，保留首輪嚴格 48/50，第二輪才是最終 50/50。 |
| 把重跑結果覆蓋首次失敗或說成全片等價 | routes 分列第 19 次單次重跑，保留 r1 日誌 PASS／像素 FAIL；r2 僅指定 50 張通過，未釐清首輪所有差異原因。 |

## 發布前覆核與發布結果

1. 提交前原定最低驗收與新增原版／畫面補審已完成；英文原版同底項購買是額外未驗證範圍，不阻擋進入提交後驗證。
2. code commit `f693086` 與 parent `d9d13e8` 已確認，提交後 default／GBK 各 115 模式 PASS、sanitizer 0，且 header 的引擎差異為空。
3. 最終 fixture 的 parent before-check 已完成：成功編譯後 14/15 案語義失敗、WE12 通過，rc 1／sanitizer 0。
4. 提交後 9 路線與片頭單次重跑的日誌／操作門檻已通過，綁定 binary `1b78aa5007758656b7e4d43e0de82ff6d24143997f98a2442729e525936d452a`。首輪嚴格 RGB 48/50 失敗保留；同 binary、種子、輸入與排程時間僅重跑一次，第二輪嚴格 50/50 通過。私人工具 M1 已修正並獨立複審，不改原工具、基準或引擎。
5. 最終證據與公開文件兩份獨立唯讀覆核均通過，未解決 High／Medium／Low 皆為 0；編寫者自查不充數。
6. 程式已快轉推送，本機、ls-remote 與 GitHub branches API 均為 `f69308620134ec09826c4aa15b938735763b7d41`。本研究以獨立文件 commit 發布；文件推送後的三方核對另存同步紀錄。

## 公開資料衛生自查

本目錄只保留 engine/API 名稱、通用 UI 類別、維護規則、VA、合成探針與結果摘要。沒有遊戲資產、執行檔、存檔、截圖、對白、腳本字串、角色名、個人路徑、帳號資訊、raw key bytes、偽代碼或 ASM。11 檔表使用儲存庫相對檔名，沒有私人證據路徑。

本節是文件編寫者自查；另有不同審查者完成最終公開文件覆核並放行，不以自查取代獨立審查。
