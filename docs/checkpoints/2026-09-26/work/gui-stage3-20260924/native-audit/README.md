> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# Production native timer helper audit

2026-09-24。獨立 headless 診斷；**直接呼叫 production `native_cas_timer_intercept`，不是 GUI／method_call／完整 bytecode 整合測試**。沒有改 production、既有 stage2 fixture 或證據檔，也沒有啟動遊戲、renderer或原 main。optimized建置與執行 exit0，表示診斷完成；以下結果是已重現的錯誤，不是功能驗收通過。

## 可重跑

從 workspace root 執行：

```sh
python3 work/gui-stage3-20260924/native-audit/build_probe.py optimized
work/gui-stage3-20260924/native-audit/runtime-probe-optimized work/stage2/game/dohnadohna.ain
```

builder由 `work/lifetime-stage2-20260924/build_probe.py` 複製，調整到較深目錄的root推導，移除自動ninja build；只讀現有compile_commands與link command，連結當前engine objects。新 `runtime_probe.c` 包含production vm.c副本（只有no-op tracing hooks）及ffi.c；原 `system4 main` 改名且不執行。`init_func_flags` 是 production 原函式，**没有像script fixture清除native攔截flags**。

讀取真實 CN AIN，按完整函式名稱＋簽名動態找 fno，不以固定數字呼叫。每次helper呼叫提供真實宣告的typed LOCAL_PAGE和argument。Rate::get在AIN中有435／436兩個同名同簽名條目，兩者皆列在stderr；本probe選最低索引的匹配條目435。helper本身只看名稱，沒有兩者分流。

## 觀察

| 真 AIN 簽名（動態找到） | 結果 |
|---|---|
| `CASTimerManager@Rate::get` f435，return FLOAT(11)，0 args | 初始返回 `0x00000001`，按float解釋為 `1.40129846e-45`，不是1.0 |
| `Rate::set` f437，return VOID(0)，arg FLOAT(11) | 輸入2.5f後getter返回 `0x40200000`／2.5f，作為raw bits對照 |
| `CreateHandle` f440，return INT(10) | 第一個handle=1，native表active=true |
| `CheckHandle` f438，return BOOL(47)，arg INT(10) | 对上述剛建立、active的handle返回0，而原script有效handle語義應為true |
| `ReleaseHandle` f441，return VOID(0)，arg INT(10) | 呼叫後native表同handle仍active=true；Check仍0，下一次Create返回2 |

來源對應：`src/vm.c` 初始 `cas_timer_rate=1` 是int，Rate getter直接写 `ret->i`；實際AIN返回型別是float，沒有可依赖的caller ITOF契約。setter复制float參數的raw bits到同一int，因此setter之後的getter對照會正確；不能把此結果描述為「getter每次都錯」或未測就認定某次GUI一定沒呼叫setter。

Create有專門分支，但Check／Release皆落到「Other Manager methods」的 `ret->i=0; return true`，不查驗handle、不改active。原AIN f438／441的已解碼bytecode會查created flags，並於Release設false及呼叫GC，見 `work/lifetime-stage2-20260920/timer-bytecode.json`、`timer-setup.md`。native的handle起點及編號可不同，本probe不僅以「下一handle不是原值」當錯誤；真正直接證據是有效Check=false及Release後原entry仍active。

## 證據與限制

- `optimized.out/.err`：原始stdout／stderr；AIN讀取保留既有Debug_SetPartsComment字元warning。
- `build-optimized.json`：production source、probe、binary hashes及實際compile／link命令。
- `harness-origin.json`：原builder來源與SHA-256。
- `result.json`：小型機讀結果。

沒有修改native helper、排除錯誤分支、替換時鐘或模拟成功回傳。此實驗没有量測GUI卡頓、Rate對GetScaled的實際效果、handle耗盡、script／native切換或遊戲操作；不能把這三項診斷直接提升為某次GUI症狀的完整根因。也沒有把它寫成script lifecycle regression：第二階段script fixture關閉native攔截，驗證的是另一條路徑。
