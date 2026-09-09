> 歷史查證／實測快照；目前交接狀態請先讀 [STATUS](../STATUS.md)。公開附件的本機路徑已去識別化。

# xsystem4 第二目標：中文對話、操作與卡頓修復

> **最新補記：使用者試玩仍卡住（2026-09-09）**
> 本報告下列通過項目只代表先前有限開場短測，不代表已穩定可玩。最新最佳化版試玩約57.765秒，記錄到8,644.006ms及5,350.551ms的present長間隔；沒有MSG對話紀錄，後段heartbeat仍在SceneTitle@Run。根因未定位，exit0或尾段持續提交畫面均不能當作成功。下一步應先重現標題頁卡住並同步記錄輸入、場景進展及停頓時堆疊，再驗收初步可玩流程。

查證日期：2026-09-09。本次沿用第一目標固定的 CN 遊戲，在獨立來源、遊戲與存檔副本中修復。**第二目標的核心操作與短程效能驗收已通過。** 中文對話能正常停等、實際單擊只推進一頁；開場測試中先前26.5秒長停頓已不再重現。整體記憶體生命週期與玩家存讀檔仍屬下一目標。

## 本次目標與驗收

第二目標是完整視窗、真正的新遊戲點擊、可讀中文對話、無輸入至少20秒停等，以及單擊只推進一頁。使用者追加的低幀率列入同輪診斷與修復。第三目標才驗收初步可玩流程、連續操作及玩家存讀檔。

| 驗收項目 | 實際結果 |
|---|---|
| 完整視窗、實際New Game | 1280×720；視窗截圖座標(169,162)的真實點擊，對應內容座標(169,130)，命中900016 |
| 中文首頁、正常停等 | 最佳化版可讀中文，無輸入43.476秒保持首頁 |
| 單擊一頁 | 一次實際滑鼠點擊，首頁MSG2/3變為下一頁MSG4/5/6；MSG是文字片段，不把5則訊息算成5頁 |
| 下一頁保持 | 再等44.333秒，文字仍在第二頁，沒有自動連翻 |
| 短程效能 | O0 130秒與O2約199秒測試沒有記錄到>1秒間隔；詳細限制見下節 |
| 最後ASan/UBSan | 231.557秒、exit0；首頁75.893秒停等，實際單擊到第二頁再停46.334秒；無ASan/UBSan錯誤；結束日誌仍有2次VM_PAGE重複釋放警告 |

（畫面附件保留於本機：首頁等待超過43秒後仍顯示相同中文）

（畫面附件保留於本機：實際單擊後的下一頁，再等待44秒沒有連翻）

[滑鼠操作紀錄](../evidence/stage2/runs/optimized-acceptance/ui-actions.json)與[停等驗收紀錄](../evidence/stage2/runs/optimized-acceptance/ui-checks.json)保存時間、訊息範圍與圖片對應。

## 已修正的直接原因

| 項目 | 原因與修正 | 驗證範圍 |
|---|---|---|
| 完整畫面與新遊戲點擊 | 新 PartsEngine API 缺省尺寸採1280×720；依CN pactex像素判定旗標做alpha命中，避免Load按鈕透明區截走New Game | 實際滑鼠與畫面；pixel fixture 14失敗→0/36 |
| CG名稱與巢狀FFI | 當前AIN的string回傳API曾錯綁舊out-pointer ABI；巢狀呼叫另保存與復原集合/receiver上下文 | 真實AIN、libffi、ASan/UBSan；context 26失敗→0/58 |
| 事件順序及短按／長按 | 本AIN先讀事件變數再Pop；鼠鍵需收到UP才放開，保留短按最低可見時間 | queue 120失敗→0/347；input 21失敗→0/56 |
| 中文文字與版面 | S_PLUSA/S_PLUSA2新模式改用兩槽lvalue；message window保留原文並獨立繪製背景與文字 | AIN6/11/14 opcode、ownership、實際中文開場；僅基本立即顯字 |
| 對話停等 | delegate引用參數補retain，與callee清理相抵；Array.Erase依真實三種宣告分流 | 真實ref bool callback與ASan/UBSan；實機40.139秒無輸入維持首頁 |
| 快捷鍵初始化 | Array.IsExist(value)曾把enum=4當成fno4，跑入錯誤初始化；改依宣告分流值查找與predicate，補callback介面參數引用 | 實際AIN、libffi與ASan/UBSan fixture通過；正式O0已越過初始化進入中文 |
| 錯誤啟用遊戲debug | 原IsDebugMode強制true，導致高頻完整VM傾印；預設關閉，僅XSYS4_GAME_DEBUG=1開啟 | AIN呼叫路徑、選項fixture；正常玩家存檔API保留 |
| GC長停頓 | cycle sweep原已停用，mark結果未被回收邏輯使用，卻重複掃描；跳過該mark，從GC完成時計算冷卻並要求配置進展 | 同一heap快照回收對照與壓力fixture通過；整組修補後長間隔改善，見下節 |
| page slot回收候選（未納入交付） | VM_PAGE=0被誤認為已在free-list，候選以free tag修正；局部測試通過，但實機暴露timer失效引用並停在logo | 保留候選patch與fixture；回退後再驗證正式範圍，不以低CPU宣稱成功 |

## 卡頓證據與比較方式

正常O0版關閉sanitizer、VM trace與frame capture後，仍量到26.508秒與6.910秒的畫面提交間隔，swap本身約1–2毫秒。卡頓內三秒抽樣的2407個main-thread樣本全部在heap_gc，其中90.28%在gc_scan_page。因此卡頓不是只由ASan造成。

較早ASan輪另有完整VM傾印熱點：三秒抽樣85.2%位於ResumeSave路徑；單份資料約232MB解壓payload。這是另一個瓶頸，不能用它解釋所有正常版停頓。

GC修後第一次O0/O2重測均暴露IsExist重載錯誤，停在開場初始化；沒有進入對話。其PERF尾段缺少後續present，不能把早段平均FPS當成改善證據，也不能把缺資料硬填成0 FPS。最終對照需使用修正該入口後的測試。另一個free-list候選雖在十萬次配置／釋放fixture中立即重用slot，實機卻停在ALICESOFT logo，且出現CASJoyClick/CASClick timer欄位指向換型或失效slot的紀錄；故保存候選證據並退出交付來源，沒有硬跳過logo或宣稱已解決所有heap問題。

| 相同O0建置條件 | 修正前 normal-perf | 修正後 normal-acceptance-perf |
|---|---:|---:|
| 實際執行時間 | 137.805秒（130秒上限＋結束延遲） | 130.177秒 |
| 已記錄的最長present間隔 | 26,507.902ms | 261.535ms（啟動階段） |
| 首MSG之後完整窗口的最長間隔 | 26,507.902ms | 33.953ms |
| 出現>1秒間隔的完整窗口 | 2 | 0 |
| 已完成PERF窗口涵蓋時間 | 88.633秒，卡住尾段未計入 | 125.027秒 |
| 對話 | MSG2/3後長卡頓 | MSG2/3正常保持；仍有2次page警告 |

交付採O2最佳化建置（保留symbols）。`optimized-acceptance` 實際滑鼠輪執行198.960秒、39個完整窗口，最長間隔81.252ms；不含文字出現窗口的首MSG後最大間隔30.270ms。與O0使用相同修補，但手動點擊時刻不同，不能當成嚴格的O0/O2加速倍率。兩者仍各有2次VM_PAGE重複釋放警告。

[效能原始統計與每窗口資料](../evidence/stage2/perf/perf-windows.json)保留完整數值和焦點flags。所有實測皆單獨執行引擎，效能輪沒有並行建置、ASan或VM trace，也關閉內建frame capture。

上述改善屬本次開場短測，沒有證明整個遊戲保持相同速度。表中normal-perf基準已關閉錯誤的game-debug，後續加入GC、IsExist等修補。這是整組後續修補的實機效果，不能把全部改善歸功於單一項，或把表中差異歸因於關閉debug dump。正常版RSS在已保存的93秒觀測為1.10GiB（1,155,936KiB）；大量物件保留與回收問題仍未解，是第三目標需要處理的項目。

測量的是SDL_GL_SwapWindow完成間隔，含重複畫面提交，不等於螢幕實際顯示率或遊戲內容更新率。p95按每5秒窗口統計，不能把各窗口p95平均成全程p95。窗口flags保留焦點資訊；ASan輪僅驗證正確性，不作一般版效能結論。LeakSanitizer本輪未啟用，因此沒有洩漏檢查通過的結論。正常、O2及ASan三個最終輪次的結束日誌均有2次VM_PAGE重複釋放警告，列為未解；sanitizer未報錯不等於這項警告已修復。

## CrossOver正常版參考

使用者提供 /Applications/多娜多娜.app 作為可正常遊玩的參考，且不要求優先處理。本輪只讀wrapper與它明確指向的遊戲目錄；未啟動或修改它。wrapper指向Wine Crossover 23.7.1-1，設定renderer=gl以啟動Windows原始引擎；未實測實際backend。AIN、ini、Version.txt（1.01）和exe與本次隔離遊戲完全相同，適合作為同一份腳本的畫面與操作基準。

「正常可玩」是使用者已觀察到的結果，本輪未獨立驗收其幀率或存讀檔。不要將Wine設定直接當成原生SDL版的修法。

## 仍未完成與下一步

本次不能證明完整遊戲可玩或沒有記憶體洩漏。正引用cycle及部分孤兒清理仍未修；完整Array API、逐字/ruby/Flat/等待圖示、所有版面與文字控制碼未全面實作。新文字sidecar未直接加入parts存檔格式，需核對load後是否由AIN重建；玩家存讀檔、backlog、縮圖也尚未驗收。

[free-list候選回退紀錄](../evidence/stage2/freelist-candidate-result.md)列出失效timer證據及尚未確認的ownership入口；它未包含在交付patch中。其他Array重載、primitive OPTION callback與標題列亂碼亦未全面處理。

修補涉及的production fixtures已通過ASan/UBSan；normal和ASan的libsys4內建測試各2/2通過。三個建置的352個來源檔manifest完全一致，並與交付來源吻合。這些局部測試不取代未走到的遊戲流程。

第三目標建議用固定短流程：連續20–30次翻頁、返回、重進至少三輪，觀察物件數和RSS；再做三個存檔點的存檔→推進→讀回→重啟載入。卡住的場景才用同AIN的CrossOver版作可見行為對照。完成這些驗收後才能稱為初步可玩測試版。

## 版本、交付與用量

隔離來源base為484f4bc128c6f62fa3cba2bf3d75dddf5d4e465b，libsys4為8c939465910499b4802ec6dd619794ca58ba4708。每輪保留binary hash、來源patch及逐檔source manifest，使用新存檔目錄。原專案、CrossOver版、原有存檔和第一目標基準保留。

- 第二目標本機重新測試（完整附件保留於本機）：雙擊啟動最佳化版；每輪全新隔離存檔，最長5分鐘，可關閉遊戲視窗提早結束。這是本機測試啟動器，依賴目前work目錄和既有Homebrew函式庫，尚非可攜式發行版。
- 交付修補（完整附件保留於本機）：只適用上述WIP基底，並非直接對fork master917f1a2套用；未push GitHub。
- [建置身份](../evidence/stage2/builds.json)、[原有副本檢查](../evidence/stage2/original-identity-final.json)、[CrossOver盤點](../evidence/stage2/crossover-reference.md)、[下一目標建議](../evidence/stage2/stage3-recommendations.md)。
- 證據目錄附production fixtures、sanitizer結果、原始engine log、CPU sample、來源manifest。保存的是測試與修補資料，沒有另複製整套遊戲資產或玩家存檔。

第二目標開始時帳戶週用量17%；最終已用64%、約剩36%，由17%增加47個百分點。百分比是帳戶共享的用量窗，不能換算成此對話精確token。未使用額度重置。
