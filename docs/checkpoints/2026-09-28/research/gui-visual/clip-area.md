# v14 ClipArea：開場與標題擦入（2026-09-30）

## 問題與修正

`4168ff8` 的 SetComponentClipArea／SetComponentEnableClipArea 及四個 getter 都是空殼，IsComponentEnableClipArea 未註冊，pactex 也未讀裁切欄位。開場文字從第一幀整段出現，標題按鈕的 ClipX motion 沒有效果。

本組補上七個 HLL 的狀態、pactex 載入、共用 parts shader 的矩形裁切，以及 XPE 記憶體存讀。CG、文字、FLAT、emitter、Flash 與經 CG 路徑呈現的元件共用裁切；原本的 alpha clipper 獨立保留。所有幾何裁切限定 AIN v14，較舊版本維持原路徑，XPE writer 維持 v3 格式。

## 原版依據

以下皆為原版 EXE 靜態反組譯及 AIN 宣告／CALLHLL 查證，不執行解殼傾印。

| 宣告 | 靜態呼叫點 | 原版實作 |
|---|---:|---|
| SetComponentEnableClipArea(int, bool) | 2 | 0x58db60 |
| IsComponentEnableClipArea(int) | 3 | 0x58dbf0 |
| SetComponentClipArea(int, int, int, int, int) | 6 | 0x58dc90 |
| GetComponentClipAreaPosX(int) | 3 | 0x58dd60 |
| GetComponentClipAreaPosY(int) | 3 | 0x58de00 |
| GetComponentClipAreaPosWidth(int) | 3 | 0x58dea0 |
| GetComponentClipAreaPosHeight(int) | 3 | 0x58df40 |

每個名稱只有一種宣告，所有 CALLHLL 的 arg3 都是 0。dispatcher 為 0x57b900，跳表 0x589714，索引 133–139。

- SetArea 只在四個值有任一改變時自動啟用裁切；設相同值不會重新打開已關閉的裁切（0x58dcde..0x58dcfe）。SetEnable 不改矩形。未知部件不建立，getter 回 0／false；矩形不夾限到部件大小。
- pactex 的 `クリップ領域` 在 0x554264..0x5542e0 逐欄讀五個整數，預設 0。enable 使用非零判定，停用時仍保留矩形。支援 SJIS 與 GBK 鍵，部分清單缺欄以 0 補足。
- **原版推翻交接的最近祖先建議**：0x579ec0 對已繼承的螢幕矩形取交集（左上取 max、右下取 min），context 複製 0x579460／0x579690 保留裁切，0x535b4a..0x535bf4 傳給子部件。因此一般部件沿全部啟用的祖先取交集。
- **原版裁切不是旋轉四邊形，也不另加 box origin offset**：0x535875..0x53589f 以目前元件矩陣投影零點，0x535a27..0x535aa7 對偏移及寬高乘自己的倍率，再用 cvttss2si 朝零截斷。原版 +0xd8/+0xdc 為自己的倍率，SetComponentMagX 0x58d56d 直接寫入。

一般 2D 遊戲座標中的矩形：

```text
O = 元件錨點矩陣 × (0, 0, 0, 1)
x = trunc(O.x + clip.x × local.scale.x)
y = trunc(O.y + clip.y × local.scale.y)
w = trunc(clip.w × local.scale.x)
h = trunc(clip.h × local.scale.y)
```

裁切本身不額外乘父層累積倍率或翻轉符號。父層變換仍可能改變 O。引擎把交集變成 screen-to-unit-rectangle 矩陣交給 shader，超出 [0,1) 的片段丟棄；空尺寸／空交集不繪製，也不對奇異矩陣求反。

## 存讀格式與相容性

AIN v14 的 XPE 版本升為 4，在各部件的 alpha clipper 編號之後保存 enable 與四個矩形值。reader 接受舊 v3，裁切預設關閉／零矩形。v13 以下 writer 仍輸出 v3，欄位順序不變。這是 xsystem4 內部格式的往返一致性需求，並非宣稱相容原版的部件二進位存檔格式。

## 探針與審查

新增 `clip-area`，案例透過真 AIN 宣告呼叫 HLL，載入合成 EX tree，使用 dlsym 取得新幾何函式；不使用遊戲資產或 GL，不建立存檔檔案。

- CA1：Set/Get／IsEnable、相同矩形不重啟、變更矩形自動啟用、未知部件不建立。
- CA2：SJIS／GBK pactex、enable=0 與非 1 非零值、缺欄補 0。
- CA3：父子裁切交集、錨點與 box origin 分離、自己的倍率與父倍率分離、旋轉仍軸對齊、朝零截斷、空交集、v13 不作用。
- CA4：v14 開啟／停用矩形記憶體往返；v13 只變 ClipArea 時完整輸出不變；v14 讀入 v3 預設停用。

獨立審查要求補上部分清單與幾何探針，再加四角驗證及不同父子倍率，均列入本組。程式碼審查為 CODE SHIP，兩個 Medium 已關閉，沒有剩餘 High／Medium。四角與中心、不同父子倍率和旋轉案例均已執行通過。

## 執行驗證

- 修正 commit：`190c1c8`，基準 `4168ff8`，libsys4 維持 `247f544`。兩組 verify 在提交前執行，因此摘要列 `head=4168ff8` 與 `diff_sha=9cefc2892404`，代表基準加上本組最終原始碼變更；提交後另做 before-check 與還原探針。
- [預設 57 模式](clip-area/verify-default.txt) 與 [強制 GBK 57 模式](clip-area/verify-gbk.txt) 均為 `VERDICT PASS`、sanitizer 0；`deleted-event` 仍為既有預期 exit 87。
- [新探針預設輸出](clip-area/probe-default.txt)／[GBK 輸出](clip-area/probe-gbk.txt)：CA1–CA4 全部通過。`sjis-chars` 的 `SJIS ` 輸出行與基準逐位元相同。
- 正式 GUI 150.267 秒，88 筆 MSG 與基準逐行相同，assertion 0、堆疊溢位 0；峰值 RSS 514,015,232 bytes，40 張 framebuffer。已視讀角色對話與據點教學：正文、人物、按鈕與教學元件均可見。基準為 150.403 秒、500,924,416 bytes；不宣稱這個單次差異代表記憶體改善或退步。[摘要](clip-area/gui-summary.json)
- `before-check.sh 4168ff8 clip-area`：4/4 案例失敗，exit 1、sanitizer 0；自動還原到 `190c1c8` 後 src/include 乾淨，新模式再跑 exit 0、sanitizer 0。[修正前摘要](clip-area/before-4168ff8.txt)
- 原版逐幀量化見下節。

## Wine 原版逐幀對照

使用既有 Wine 原版 `intro/` 的逐幀 JPG（擷取後移除上方 52 px 視窗框）與本組修正前後各 160 張 0.1 秒間隔的 framebuffer PNG。畫面有不同啟動延遲，以動畫階段對齊，並不以檔名當共同的絕對時間。

| 檢查 | 修正前 | 修正後與原版 |
|---|---|---|
| ALICESOFT 文字右界 | t10–21 皆為 x=916，從一開始整段出現 | after t15 尚無字，t16–22 為 559→730→869→903→912→915→916；Wine w051 無字，w052–060 為 547→650→827→888→907→913→915→915→916，均由左向右擦入 |
| 標題按鈕 | 按鈕過早整段出現 | 左側按鈕從右向左展開，右側按鈕從左向右展開，各列依序延遲，與原版方向一致 |
| 角色對白／據點 | 88 筆 MSG | 88 筆逐行相同；已視讀對白正文、人物、據點教學與底列，沒有看到新增的元件消失 |

這是擦入行為與既有畫面的回歸比對，**不是逐像素等同**：Wine 標題擦入時還有粉／金閃光，本版沒有相同閃光；啟動時間、取樣相位及既有 Motion 終值亦有差異。兩版 intro 第 0 張均有初始完整 LOGO，像素相同，不能宣稱這個既有起始影格已修掉。延遲期間的文字提前顯示已消失。截圖與 OCR 資料只留本機，不進公開 repo。

## 限制

- 原版 CGUIComboBoxView 的內部子元件會清空繼承裁切（0x532fce，+0x24c=0）；一般部件建構預設為 1。移植尚未建立此內部 view，這個例外未實作。
- 原版額外 pivot 欄位 +0xa0/+0xa4、3D 相機及非標準 render-target 比例未驗證；不能把一般 2D 公式宣稱為全部場景完全相同。
- 負尺寸原版後端行為未追到；本組保留 HLL 的原值，繪製時視為空矩形。矩形、alpha clipper 及其他部件屬性的完整存檔相容性仍有既有缺口。
- 輸入卡死、春銷 Start 斷言、system.Reset 與長時間穩定性不屬於本組，尚未處理。
