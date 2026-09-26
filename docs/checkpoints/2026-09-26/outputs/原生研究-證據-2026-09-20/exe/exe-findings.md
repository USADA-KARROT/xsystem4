> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 本機原版引擎靜態研究：已取得可用的 HLL 入口與語義證據

2026-09-20。範圍是既有本機檔案的唯讀靜態分析；沒有執行遊戲、安裝工具、修改原 EXE 或重製整份 binary。以下位址都是 preferred VA，image base 為 `0x400000`。函式名稱來自主研究的真實 AIN HLL0 metadata，不能單靠 binary 字串或 import 推論 API 語義。

## 結論

可深入研究，且這輪已超過只盤點檔案：使用者保留的 `dohnadohna_dump_SCY.exe` 有可解碼的 C++ 程式、HLL 分派表與 RTTI。已抽出 Array 84 格、PartsEngine 868 格入口，並由反組譯確認 Array 的兩種 IsExist、Erase(predicate) 的首個命中刪除，以及 PartsEngine clickable 欄位存取。這些可作為另寫原生引擎的行為規格證據。

這不等於已有原始碼，也不等於新 macOS 引擎已實作；dump 的生成流程與現用受保護 EXE 的逐位元對應尚未獨立證實。完整 VM、delegate lifetime、event scheduling 與端到端換頁仍待研究。

## 檔案身分與保護狀態：事實

| 檔案 | 大小 | SHA-256 | 可直接讀核心程式？ |
|---|---:|---|---|
| game-workcopy/多娜多娜 一起幹壞事吧/dohnadohna.exe | 4,188,176 | `97020c28f1e481eed5a8c28986c92fc1cbf092434780a6c6a055aff280c9377a` | 主要區域不適合直接反組譯 |
| Downloads/dohnadohna_工作檔案/dohnadohna_dump_SCY.exe | 15,474,176 | `211ce63e0e229fa7d77b2af141149681ec37521e7497914473c739569aa101f0` | 本輪分析主體 |
| Downloads/dohnadohna_工作檔案/dohnadohna_final6.exe | 12,273,664 | `f0a611c52db10271fa104acb8e8f78dd635c10b8ee1456b488d490aeb3fd0e80` | 前段核心仍是 packed bytes |

現用 EXE 是 PE32 x86 GUI，10 sections，8 個無名稱；主要 sections entropy 7.98–8.00，多數具有 RWX 權限。Entry RVA `0xec32dc`，位於末端 `.data`；import 僅 13 DLL、16 項，例如 GetProcAddress / LoadLibraryA。沒有 COFF symbols、debug directory 或 PDB 路徑；目標 HLL 字串無匹配。這一組證據強烈支持「核心被打包／保護」，不能單由 entropy 判定特定廠牌。

SCY dump 的前段 section raw bytes 已展開，首段 entropy 6.58；有 Array、PartsEngine、system、Delegate、Sys43VM 名稱，並匹配到 265 筆本研究關鍵字的 RTTI 名称。這不是 RTTI 總數。例：`CArrayPage@sys43vm`、`CDelegatePage@sys43vm`、`CFuncPage@sys43vm`、`CJaffaVM@sys43vm`、`CGUIMessage@partsengine`、`CGUIMessageWindowModel@partsengine`。

現用 EXE 與 dump 的 COFF timestamp 均為 `2020-09-03T08:41:46Z`，多個 section RVA/virtual size 相同；這是關聯線索，不能證明 dump 未被修補。Dump imports 含部分 `?` 未解析名稱；不得視為完整重建後的可執行程式。

`final6` 在研究的 9 個核心範圍有 virtual allocation，卻沒有相應 file-backed bytes。因此這輪不能比較那些原生片段是否一致。其前 7 個 packed raw sections 與現用 protected EXE **7/7 逐位元相同**，詳見 `dump-final6-comparison.json`；這只能證明保護後檔案的這些區域相同。不能把 RVA 當 file offset 硬比對。

## HLL dispatch：確證的位址關係

| Library | 名称字串 VA | 比較／分支 VA | 成功回傳的 dispatcher | 索引上界 | Jump table VA |
|---|---|---|---|---:|---|
| Array | `0x7cedbc` | `0x494747` | `0x644300` | 83 | `0x644f18` |
| PartsEngine | `0x7cef20` | `0x4948f7` | `0x57b900` | 867 | `0x589714` |

字串比較 helper `0x41bfa0` 讀取名稱長度與內容並回傳相等旗標；兩個分支在相等時將上表 dispatcher 位址載入 EAX 後返回。Dispatcher 依函式索引 switch。這是「字串 → 程式 xref → dispatch → 函式索引」的連續證據，不只是名稱猜測。

`Array-jump-table.json` 與 `PartsEngine-jump-table.json` 提供完整 index → VA。`validated-jump-tables.json` 對每一格提供 RVA、file offset、section index/flags 與前 8 bytes；952/952 都落在有檔案資料且標示 executable 的 section。這個檢查只確認位址有效範圍，不能代替每個 API 的語義驗證。

## Array：已讀到的行為

### IsExist 的值與 predicate 分別走不同入口

| AIN index | 宣告 | 分支 VA | 底層主要 helper |
|---:|---|---|---|
| 56 | IsExist(value) | `0x644bd5` | `0x648a80` → `0x648b00` |
| 57 | IsExist(predicate) | `0x64473a` | `0x648c40` → `0x648cc0` |

兩支皆用 `test eax,eax; setns` 將搜尋結果是否非負轉成 bool，並寫 VM return-value type 47。兩者最後共用 `0x646bb0`，其反組譯可直接確認：

1. 搜尋起點下限為 0，終點上限為 array length。
2. 每次對目前 index 執行比較器；第一個 true 立即回傳該 index。
3. 到終點仍未命中則回傳 -1。

值分支的比較器工廠 `0x646020` 依元素型別建立不同比較器；其中一個 vtable `0x810eb4` 的 call operator `0x64bcd0` 是讀取值後 `cmp` / `sete`。Predicate 分支使用另一個工廠 `0x6461e0`。本輪沒有完整還原其 delegate capture / retain 契約，不能據此宣稱所有 element types 已理解。

### Erase(predicate) 是刪第一個命中元素

AIN index 15/16/17 分別對應 `(index,length)`、`predicate`、`source array`；jump table 分支分別是 `0x644519`、`0x644572`、`0x6445bd`。

Index 16 在 `0x644596` 呼叫相同 predicate-first-find `0x648c40`，只得到一個 index；隨後於 `0x64459b` 將該 index 傳给 array vtable `+0x5c`，沒有再次搜尋的外層 loop。

進一步用 MSVC RTTI 連結到 `CArrayPage@sys43vm`：TypeDescriptor `0x87bb34` → Complete Object Locator `0x82c544`（interface object offset `0x20`）→ vtable `0x811c28` → slot `+0x5c` 的 `0x67f750`。該方法明確：

- index < 0 或 index ≥ length 時回傳 false。
- 成功時搬移後面的 elements，再把 storage resize 成 `(length - 1) × stride × 4` bytes。
- length = 1 時使用清空分支。

因此「Erase(predicate) 刪首個命中，未命中 false」有完整靜態支持；它不能直接當成 EraseAll 實作。元素析構 helper `0x680270` 尚未完整還原，複雜型別的釋放責任仍是未知。

## PartsEngine：已讀到的行為

- Index 179 `Parts_SetClickable` → `0x57e142` → `0x58f830`：取 parts id 與 bool，lookup 成功時寫物件 byte `+0x1a4`。
- Index 180 `Parts_GetPartsClickable` → `0x57e172` → `0x58f8c0`：讀同一 byte；lookup 失敗回傳 false。
- 這對 helper 結尾有成對的 atomic reference decrement 與 virtual destruction calls，支持引擎有明確物件生命週期管理；不足以外推為 VM delegate 的 retain 規則。
- Index 22 `GetClickNumber` → `0x57bd7e`：取得 manager 子物件，必要時呼叫初始化 helper，讀其 `+0x2c` 欄位。這尚不能判定 click number 在何時清零、滑鼠按下／放開的界線或 frame consumption 規則。
- Index 221 `GetButtonCGName` → `0x57eafb`、index 505 `GetMessageWindowCGName` → `0x582d88`：本次 AIN 中均為 **string(int)**，一個參數。兩支只取 arg0，產生暫存 string，再呼叫 `0x657cc0` 寫 dispatcher return object；helper 明確寫 type 12 及 string handle，接著清理暫存。這支持回傳 string 的介面，沒有 caller 傳入的字串 out-parameter。Native C++ 內部仍使用暫存／隱含返回物件，不能把那個 C++ ABI 誤認為 AIN 宣告中的 out-param。

## 未知與後續研究界線

1. 既有 dump 與目前 protected EXE 的來源鏈與完整核心一致性沒有驗證；先將這份 dump 當獨立、具 hash 身分的參考版本。
2. 952 格入口是定位結果；只有本文列出的分支有人工語義判讀。不能把格數寫成「已還原 952 個 API」。
3. 此輪未還原 VM instruction dispatcher、closure environment capture、delegate lifetime、serializer、thread/event scheduling、draw/input ordering；它們可能決定新原生引擎能否正確換頁。
4. `System` 大写名稱與小写 `system` 應區分；只有與 registry code 有 xref 的小写名稱才是這裡的 HLL library。GetCGName、IsExist 的 standalone 字串缺失不表示功能缺失，數字 dispatcher 已顯示相反。
5. `<USER_HOME>/Downloads/DohnaDohna` 實際不存在。旧 handoff 宣稱另有 JAST clean PE，這輪未取得可獨立核對的該檔案，未把舊文字宣稱當事實。

## 重跑

使用系統 Python 3 與 `/usr/bin/objdump`（Apple LLVM 21）；未使用 capstone / pefile，兩者本機系統 Python 未安裝。

```bash
python3 work/research-20260920/exe/inspect_pe.py '<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/game-workcopy/多娜多娜 一起幹壞事吧/dohnadohna.exe' work/research-20260920/exe/pe-evidence.json
python3 work/research-20260920/exe/inspect_pe.py '<USER_HOME>/Downloads/dohnadohna_工作檔案/dohnadohna_dump_SCY.exe' work/research-20260920/exe/dump-pe-evidence.json
python3 work/research-20260920/exe/inspect_pe.py '<USER_HOME>/Downloads/dohnadohna_工作檔案/dohnadohna_final6.exe' work/research-20260920/exe/final6-pe-evidence.json
python3 work/research-20260920/exe/extract_hll_evidence.py
python3 work/research-20260920/exe/compare_derivatives.py
```

每份 `.asm.txt` 首行保留精確 objdump 命令與輸入路徑。完整資料位於本目錄的 JSON、工具和有界反組譯摘錄。
