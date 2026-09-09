# Normal O0 量測：長卡頓已重現

來源 `work/stage2/runs/normal-perf`，PID36476。一般版無sanitizer、無STAGE2_TRACE、無frame capture，PERF開啟，optimization=0；仍使用`--echo-message`作MSG定位，以及20秒單次auto click `(169,130)`。runner預設130秒，實際137.805秒、runner停止、exit0；exit0不能代表流暢或完整通关。

## 可重現的長幀

| PERF窗口結束 SDL ms | 窗口 ms | 完成present數 | 平均present/s | p95 / max interval ms | max swap ms |
|---:|---:|---:|---:|---:|---:|
| 55,695 | 5,135.116 | 1,642 | 319.759 | 6.999 / 198.878 | 7.646 |
| 82,266 | 26,571.971 | 3 | 0.113 | 26,507.902 / 26,507.902 | 2.055 |
| 89,177 | 6,910.256 | 1 | 0.145 | 6,910.256 / 6,910.256 | 1.945 |

26.5秒與6.9秒的間隔由相鄰完成present直接計時；這不是ASan額外成本。這兩個window的swap本身約1–2ms，停頓主要發生在present前的工作。

較早窗口會達到159–589 present/s、vsync=0；可見這個版本會高速提交畫面，不能把這個數字當作內容更新率，或據此宣稱「高FPS所以流暢」。13個完整window只覆蓋約0.544–89.177秒；最後reported window後到程序停止的一段缺少後續完整報告，**沒有硬算成0 FPS，也沒有填入平均**。全程weighted平均只有已報告的部份，不宜當作使用者體驗指標。

window_flags早段`0x626`、之後`0x226`與`0x26`；均有SHOWN且非MINIMIZED，後段缺INPUT_FOCUS。需在optimized對照保留/註記相同焦點條件；旗標不能證明沒有被其他窗口遮住。

## 同一長卡頓內的取樣

08:33:33.565 +08（程序啟動約75.7秒）對該PID進行3秒sample，落於上列26.57秒報告窗口內。

- Main thread 2407/2407 樣本位於 `heap_alloc_slot → heap_gc`；`gc_scan_page` 2173/2407 = 90.28%。沒有該thread的ResumeSave或SwapWindow樣本。
- 這是本次長幀的明確CPU線索，和先前ASan另一時段的ResumeSave熱點不同。不能只憑單一取樣把全程瓶頸都歸因存檔。
- source `src/heap.c:505–518`：free slots<1024且heap>=4M時，每次allocation均可跑GC；`:381` 的cycle sweep被 `&&0` 禁止；`:308` 的periodic入口直接return。這可導致花時間mark卻回收不了cycle，特別需要確認滿heap時的GC重試節奏。未擅自打開不完整的sweep；root/另一agent另審pacing。

樣本與計數：`normal-36476-sample.txt`、`normal-sample-summary.json`。

## MSG定位限制

只有MSG2、MSG3，在engine.log第77、78行，位於PERF t30557與t35557兩行之間。可說它們在這兩份PERF記錄之間出現，**沒有記錄精確firstMSG時間**。`parse_perf.py` 輸出每window、原始line、前/後PERF界線、phase_by_log_order，與最近MSG index；不複製對話內容，不將window p95平均冒充全程p95。

下一步用同source optimized/O2版重測同起點，再對GC pacing修正前後比較長幀與CPU sample。要保持功能驗收：不能以取消必要存檔、禁用任務或改動等待條件換取較高present rate。
