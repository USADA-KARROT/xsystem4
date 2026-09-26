> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 9/24 delegate 自身 Clear 邊界

Production source 未修改。新增 fixture 位於 `../lifetime-stage2-20260924/delegate_reentrancy_fixture.inc`；需 forward declare `static void delegate_reentrancy_probe_step(void)` 並由 `probe_step` 呼叫，在 include fixture 後呼叫 `test_delegate_reentrancy()`。

## 已驗證

optimized 與 ASan 各 20 輪均 exit 0。每輪執行原 AIN f20752 → delegate248 → f36081，以空 ExecuterCollection 輸入進入原 true 分支。seed 直接將 owning delegate 放進 observer；未覆寫 callback、AIN 或 callback 輸出，亦未執行 observer 初始化函式。進入 callback 首條 opcode 前，harness roots 已釋放，各 ref=2（delegate + frame）；hook Clear 後各 ref=1（僅 frame）。callback 原指令共 64 步，RETURN 前直接觀察 env[2] 與 caller End 均為1，RETURN後首個 caller 指令前 obj/env refs 均0。原 observer IsEnd 和 globals 寫回正確，teardown live0。沒有為通過測試補引用或補釋放孤兒物件。

ASan 使用 `ASAN_OPTIONS=detect_leaks=0:abort_on_error=1`，stderr 只有既有 AIN 函式名稱警告，無 sanitizer 報告。live0 是獨立的 VM heap assertion，不將此結果稱作 LeakSanitizer 通過。命令及來源hash記錄於 build-*.json、evidence.json；build_probe.py 不呼叫 ninja 建置，僅使用 root 已 fresh 的 production objects，ninja -t commands 僅取得 linker 命令。

重跑：

```sh
python3 work/delegate-review-20260924/build_probe.py optimized
work/delegate-review-20260924/runtime-probe-optimized work/stage2/game/dohnadohna.ain reentrancy
python3 work/delegate-review-20260924/build_probe.py asan
ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 work/delegate-review-20260924/runtime-probe-asan work/stage2/game/dohnadohna.ain reentrancy
```

## 限定靜態審查

- `delegate_clear` 先 snapshot 全部 tuple、nr_vars=0，再釋放 snapshot。這能避免 release 重入再次 Clear 時重複釋放原 entry。`delegate_erase` 先移位並減 nr_vars，保存 env，release 後立即 break；本函式不再讀取原 page。
- `function_return` 先複製 frame、pop call stack 再呼叫 `unref_call_frame`；後者再次 snapshot 所有 owner，清除原 pin 欄位，再釋放 obj/env/local。故 destructor 重入覆写 call-stack slot 不會改掉待釋放的值。正常 callback Clear 的此路徑已動態通過。
- timeout unwind 先保存 obj/env/page，再 pop frame 並各 exit_unref；vm_free 也各釋放 delegate pins。exit_unref 不跑 destructor，因此這些路徑沒有相同的 destructor 重入。它們只經靜態核對，沒有強迫 timeout、vm_free 中斷 callback 的動態證據。
- scenario_jump/scenario_call 透過 unref_call_frame 釋放 pins，但沿用先掃 frame、最後才重設 call_stack_ptr 的舊順序；析構子若在 flush 過程重入或跳轉 scenario，需要獨立驗證。不能由本測試推定所有 unwind 安全。
- 原始 page* API 不額外保活 delegate page 本身；Clear 的被釋放物件若間接持有該 page 最後 owner，page* 返回值可能失效。此次測試 observer 仍持有 notify，所以未觸及這個最後-owner/析構重入邊界；未將未重現的風險稱為已證實 bug。
- 未擴大驗證 save/resume、相同 obj/fun 不同 env 的 identity、強引用循環或一般 assignment。限定可確認的正常自 Clear 邊界未發現新的 production bug，不因靜態可能性私改 source。
