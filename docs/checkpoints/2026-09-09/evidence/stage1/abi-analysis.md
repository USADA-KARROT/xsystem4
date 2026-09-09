# 第一個 sanitizer 阻塞：Parts_GetPartsCGName ABI 錯配

日期：2026-09-09。**已確認 WIP 把 Dohna v14 的「兩整數參數、回傳字串」函式，綁到「三參數、out string 指標、void 回傳」舊 C 函式。這不是單純參數換序。** AIN 宣告、實際 bytecode callsite、WIP 綁定與 sanitizer stack 四項證據一致；Rufim 已有針對此差異的適配器。

本次僅靜態讀碼，另以不執行 bytecode 的小型 libsys4 metadata reader 讀取 `game/dohnadohna.ain`。沒有修改引擎、遊戲資料、啟動遊戲或操作 UI。

## 1. 當次 runtime 證據（root 統一 runner）

`work/stage1/runs/sanitizer-start/engine.log:52–84`：

- UBSan：`src/parts/parts.c:1279` 讀取 misaligned address `0x000000000001`，期待 `struct string *` 的 8-byte alignment。
- 首個 C frame：`PE_GetPartsCGName` → libffi → `hll_call`（ffi.c:679）。
- 遊戲目前函式：fno 9648，`parts::detail::CCGParts@CGName::get`；IP=`0x2FC070`。
- 呼叫來源包含 `AdvEventCg@Set` → SceneAdv → DohnaDohna@RunGame。
- Root 回報 process 約48.605秒 exit134；時間／exit以統一 runner 記錄為準，本 agent 未另跑遊戲。

此為第一個已捕捉的 undefined-behavior 阻塞，不是已證明先前所有 VM_PAGE double-free 都源自同一個問題。

## 2. 從本次 AIN 直接取出的宣告與 callsite

AIN version=14，37,742 個 functions、36 個 libraries。相關 HLL 宣告：

```text
library 27: PartsEngine
function 699: Parts_GetPartsCGName
return_type = 12 (AIN_STRING)
argc = 2
arg[0] Number: type 10 (AIN_INT)
arg[1] State:  type 10 (AIN_INT)
```

函式 fno9648 的最小 bytecode：

```text
0x2FC054 PUSHSTRUCTPAGE
0x2FC056 PUSH 1
0x2FC05C X_REF 1
0x2FC062 PUSHSTRUCTPAGE
0x2FC064 PUSH 2
0x2FC06A X_REF 1
0x2FC070 CALLHLL 27 699 0
0x2FC07E RETURN
```

兩個參數由 struct member 1／2 取值；callsite IP 完全吻合 runtime dump。這裡未從靜態 bytecode 推算實際 State 值；`0x1` 是 runtime 錯誤所觀察到的實參值。

證據：`work/stage1/ain-hll-signature-evidence.txt`。只讀前後 AIN SHA-256 均為 `beefa6677237424a9e6dfb304299e5d47c524fb2956e1dd80aed113133fd8947`。

Probe 來源／產物：`ain_hll_signature_probe.c`／`ain_hll_signature_probe`，只呼叫 ain_open、列出目標 HLL metadata／兩個同名 getter 的短 opcode 片段、ain_free；不執行 VM、SDL 或 HLL。它連結此次普通 WIP 的 libsys4 static library，沒有更動原倉庫。

## 3. WIP 的錯配機制

WIP `src/hll/PartsEngine.c:780`：

```c
HLL_EXPORT(Parts_GetPartsCGName, PE_GetPartsCGName)
```

`src/parts/parts.c:1273`／`include/parts.h:39` 真正的 C 簽名：

```c
void PE_GetPartsCGName(int parts_no, struct string **cg_name, int state);
```

`PartsEngine_PreLink`（hll/PartsEngine.c:1586–1638）有其他 v14 adapter，但沒有替換這個 getter。對相關 prelink/stub 檔搜尋，也沒有找到額外覆寫此綁定。

WIP `src/ffi.c:1098–1123` 以 **AIN 宣告**建立 cif：nr_args=2、參數型別兩個 sint32、回傳型別 pointer；名稱連結（:1137–1139）則指向上述舊 C 函式。C 編譯器無法在這種 function-pointer／libffi 動態連結中驗證真實簽名一致。

因此呼叫時：

| 位置 | AIN／libffi 實際傳送 | C callee 如何解讀 |
|---|---|---|
| 第1參數 | Number:int | parts_no，正確 |
| 第2參數 | State:int，當次觀察為1 | struct string **cg_name，被解讀為0x1 |
| 第3參數 | 不存在 | state 讀到未指定值，行為無定義 |
| 回傳 | 呼叫方要求 struct string * | C 是 void，回傳值亦無有效保證 |

當 callee 繼續到 `if (*cg_name)`（parts.c:1279），即讀取位址0x1，與 UBSan 完全吻合。這個不合法指標可由參數錯配本身產生，不必先假設 heap 早已損壞。第三參數與回傳也錯，因此不能只在讀指標處加防護就認為 ABI 已修復。

## 4. Rufim 的對應解法

引入 commit：[`1f312c5db8603705c18fa9d18aefcb1c7e265c34`](https://github.com/Rufim/xsystem4/commit/1f312c5db8603705c18fa9d18aefcb1c7e265c34)，2026-08-05；提交說明即討論新版本 string getter 改成直接回傳。

Rufim `src/hll/PartsEngine.c:5994–6000`：

```c
static struct string *PE_GetPartsCGName_ix(int parts_no, int state)
{
    struct string *out = NULL;
    PE_GetPartsCGName(parts_no, &out, state);
    return out ? out : string_ref(&EMPTY_STRING);
}
```

Rufim 保留原三參數 helper，增加符合新 ABI 的二參數 wrapper；沒有 CG 名稱時回傳合法空字串 reference。PreLink（:7097–7111）對 `GetPartsCGName` 與 `Parts_GetPartsCGName` 兩個名稱，在 AIN `return_type.data == AIN_STRING` 時替換成 wrapper，使舊遊戲的 out-parameter 形式仍保留。

本次已核實 Rufim 有此程式，不代表已在本機 CN 遊戲驗證其修補成功。

## 5. 下一階段最小修復／驗證入口（本次未實作）

1. 先以這個已確認 signature 作最小適配，參考 Rufim wrapper／PreLink 方法；適配條件可再明確確認 AIN_STRING、argc=2、兩個 AIN_INT，保留舊三參數 out-string 綁定。
2. 保留返回字串的所有權語意：正常 CG 回傳獨立／有效 reference，無 CG 時回傳合法空字串。直接 null 或 void 返回不符合 WIP ffi.c:755–759 的 string-return 處理。
3. 用相同 sanitizer binary 設定／同一 input sequence 重跑最小場景，確認穿過 fno9648／AdvEventCg@Set 原阻塞位置且不再有 ABI UB；再記錄下一個實際阻塞，不能先宣稱整體對話或存檔完成。
4. 如有舊版遊戲／fixture，再驗舊 out-string 形式仍有效。其餘 PartsEngine string getters 可依 AIN 型別與 C 簽名另做小型 audit，但先不要把 Rufim 全部 149 行變更或整個 input/VM 重構一起合入。

這是一個有直接 runtime＋metadata 證據、可單獨驗證的下一階段修復點，比全面升級或繼續猜測 timer 更具可追蹤性。
