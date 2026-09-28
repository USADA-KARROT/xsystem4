# 成就通知文字元件：研究與驗證

基準為 `2167bbc`，libsys4 固定 `8c93946`。本組只處理低階部件的文字／CG 狀態辨識與文字樣式初始化，不改 AIN、不繞過斷言、不硬編成就名稱，也不更換 HLL 綁定。

## 根因與呼叫形狀

`SceneAchievementNotify@0`（f34351）透過 IActivity 的 GetText 取得通知文字。`CActivityWrap@GetText`（f581）預設 state=1，交給 `GetText#2`（f583）；後者呼叫 `CompParts`（f534），要求型別 21。f534 先驗名稱、查元件編號，再比較 `GetComponentType(number,state)`。失配時 f583 在 bytecode `0x233f6` 回空介面 `[-1,0]`，因此「nonnull」失敗不代表名稱不存在。

工作副本的 Pact 靜態解析確認：`TextAchievement` 存在，外層是「低等級部件」，普通狀態為「文本部件」，移入與按下狀態都是空的「ＣＧ部件」。舊載入器只儲存元件層級的 0／1，而 getter 忽略 state。這條路徑不經 regex、GetUser 或 QuickSort。

下表是完整 CN AIN dump 中的靜態 CALLHLL 位置數，不是執行次數；各名稱只有一種宣告，所有 arg3 均為 0。

| PartsEngine 宣告形狀 | 宣告數 | CALLHLL 處數 | wrapper |
|---|---:|---:|---|
| bool ReadActivityFile(string,string,bool) | 1 | 1 | f649 |
| bool IsExistActivityPartsByName(string,string) | 1 | 1 | f651 |
| int GetActivityPartsNumber(string,string) | 1 | 1 | f653 |
| int GetComponentType(int,int) | 1 | 1 | f9147 |
| void SetComponentType(int,int,int) | 1 | 1 | f9146 |
| string GetUserComponentName(int) | 1 | 7 | 非此 GetText 路徑 |
| void SetUserComponentName(int,string) | 1 | 2 | 非此 GetText 路徑 |
| string GetActivityPartsName(string,int) | 1 | 1 | 非此 GetText 路徑 |

上層直接呼叫通用 wrapper 的 CALLFUNC 次數：GetComponentType 29、GetPartsNumber 98、IsExistPartsByName 2。本組相關 C 原型與宣告相符；不是同名 overload 的 ABI 錯配。

## 原版靜態依據

只反組譯 EXE 傾印，沒有執行原版。

| 語義 | 原版位置 |
|---|---|
| PartsEngine dispatcher／跳表 | `0x57b900`／`0x589714` |
| ReadActivityFile 讀三個參數並交給 activity loader | case29 `0x57bfcb` → `0x5638f0` → `0x560210` |
| GetActivityPartsNumber | case39 `0x57c435` → `0x58ab40` |
| GetComponentType 依 state 委派；找不到元件回 -1 | case75 `0x57cd1a` → `0x58b660` → `0x5363f0` → `0x564c30` |
| 外層只允許型別 0..18；18 才建立各低階狀態 | `0x5392ec`；18 分支 `0x539521` → `0x56a4a0` → `0x5b8bc0` |
| 依名稱讀普通／移入／按下狀態 | `0x5b8cc0`／`0x5b8de5`／`0x5b8ee4`，分別為 state 1／2／3 |
| CG 型別恆為 19，與 CG 名稱是否空白無關 | vtable `0x810048` +8 → `0x533d80`，回 `0x13` |
| 文字型別恆為 21 | factory `0x5b7906` → `0x5beb40`；vtable `0x810334` +8 → `0x4df5b0`，回 `0x15` |
| 型別字串表及新版字串解碼 | `0x4eda70`、`0x5b8f90`、`0x5b9040`、`0x5b9051` |
| 主文字裝飾與 ruby 分別讀取 | loader `0x5c2d20`；主文字 `0x5c34a8` → decoder `0x5bb470`；ruby `0x5c3521` |
| 裝飾的字色／邊色／字型／字級 | `0x5bb4a5`／`0x5bb50d`／`0x5bb588`／`0x5bb5fb` |
| 粗細／邊線／字距／行距 | `0x5bb6e9`／`0x5bb75e`／`0x5bb85e`／`0x5bb8c0` |

實際資源的主文字為字級 32、字距 -2、行距 12；ruby 為 8、0、3。實作只讀主文字裝飾的直接子欄位，不能遞迴搜尋到 ruby。

## 修正與反駁紀錄

- 只對已辨識的低階部件與具名 CG／文字狀態建立標記。文字回 21，CG 回 19，空 CG 也建立狀態。其他型別保留既有流程與 raw getter，沒有新增 VM_ERROR。
- 標記設在 `struct parts`，不放進會被 reset 清零的 `parts_state`。已標記 CG 轉為文字後，getter 跟隨實際 state；未標記元件不因某次字型 setter 就改變 widget 型別。
- 顯式 SetComponentType 清除 loader 標記，保留原有 raw setter 的優先權。這是避免回歸的相容決策，不代表本組已重作原版 setter 全部語義。
- 獨立反駁者指出：未知分支若沿用序位，可能覆蓋先前按名稱建立的狀態。修正改成先處理舊 fallback，最後再建立明確認出的具名狀態；另加帶 surface-area 的碰撞測試。
- 文字裁切使用文字 setter，避免 CG setter 把文字狀態重建成 CG。未知型別、無效 state、v13 行為均有相容性案例。

## 驗證證據

`before-check.sh 2167bbc activity-text`：具名查找成功，但三個 state 都回 0，預期依序為 21／19／19；exit 86，sanitizer 0。見 [修正前摘要](before-2167bbc.txt)。

修正後 [activity-text 摘要](after.txt) exit 0；[完整 34 模式](verify-summary.txt) `VERDICT PASS`，sanitizer 0，deleted-event 維持預期 exit 87，其餘 exit 0。探針把正式 parts／activity 原始碼複製到 repo 外，只附加 static 入口 wrapper，使用正式 loader、真 AIN 與真 libffi。Fixture 為合成資料，不含遊戲文字或資產。

[GUI 摘要](gui-summary.json)：同為 150 秒上限，基準在 120.865 秒因成就斷言停止；修正後跑至 150.367 秒的時限，assertion 0、堆疊溢位 0，MSG 仍為 88。後段 heartbeat 已進入 `SceneAzito` 的場景迴圈，但未驗證據點互動。兩次皆檢視 `xsys4_t39.png`：背景、人物、框體與頭像可見，角色對白仍空白。截圖只保留在本機；成就通知的實際顯示與消失動畫尚未驗證。

峰值 RSS 由約 1.89 GB（基準提早停止）變為約 2.01 GB（跑滿時限）；時間不同，不能直接當作記憶體回歸證據，也不能宣稱記憶體問題已解決。

## 未驗證及未涵蓋

- 原版無效 state 的安全回傳值未驗證；現有 fallback 只是不新增移植端回歸。
- SJIS 名稱分支保留，但本組合成資源以 GBK 測試；不能宣稱已驗證日文版遊戲。
- 字型預設值、字型轉換與所有文字對齊規則未完全驗證；缺欄位時沿用引擎預設值。
- PE_Save／PE_Load 原本就未序列化 raw component_type，本組 marker 也不序列化；活動型別的存檔往返未驗證。沒有改動既有存檔格式。
- 第二階段的角色對話文字，以及 GetStructPageList／DeserializeStruct 持久化，須各自驗證，不能由本組通過推論已完成。

## 重跑靜態調查

環境變數指向使用者自行提供的唯讀資料。下列工具不執行 EXE、不寫遊戲目錄；輸出放在 repo 外。

```sh
# 從 clone 根目錄執行；先設定 XS4_EXE_DUMP、XS4_LIBRARIES_DUMP。
R=docs/checkpoints/2026-09-28/research/achievement-text
# 使用有 capstone 的 Python；輸入為唯讀解殼傾印與 libraries.txt。
python "$R/pe_dis.py" table GetComponentType ReadActivityFile
python "$R/pe_dis.py" 0x4df5b0 8
python "$R/pe_dis.py" 0x533d80 8

# Pact 工具只讀工作副本。
cc -I subprojects/libsys4/include "$R/pact_fields.c" \
  "$XS4_WORK/build-opt/subprojects/libsys4/libsys4.a" -lz -framework CoreFoundation \
  -o "$XS4_WORK/pact_fields"
"$XS4_WORK/pact_fields" "$XS4_GAME/dohnadohnaPact.afa" > "$XS4_WORK/achievement-fields.tsv"
```

`pe_dis.py` 的輸入環境變數為 `XS4_EXE_DUMP`、`XS4_LIBRARIES_DUMP`。`pact_fields.c` 只擷取該元件的型別與字型欄位，不輸出遊戲對白或完整封存檔。
