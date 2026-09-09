# 第二階段：點擊路徑、訊息契約與佇列修復

日期：2026-09-09。靜態審核由 WIP 484f4bc 的 stage2 隔離來源開始；root 同時加埋點與執行遊戲。本項沒有啟動遊戲／UI，也沒有修改 parts/input.c。經 root 授權，在 **src/hll/pe_v14_message.c** 實作以下已核實的佇列與 getter ABI 修復；後續再授權 **src/input.c** 的最小 pending-release 修復，見第9節。

來源路徑：

- W：`<WORKSPACE>/work/stage2/source`。
- W0：`<WORKSPACE>/work/stage1/wip-source`，固定未修版本；下列 input 行號優先使用 W0，避免 root 埋點令行號移動。
- R：`<WORKSPACE>/work/stage1/rufim-source`，589cf2c。
- AIN：`<USER_HOME>/Claude/projects/dohna-cn-dump/ain_code.txt`；HLL 宣告在同目錄 `libraries.txt`。

## 1. 最重要的判斷：灰黑轉場不等於新遊戲沒有命中

新遊戲按鈕實際 callback 是 **36741 → SceneTitle.Start(31808) → TitleClose(31811)**。TitleClose 會放大／移動主畫面、建立黑色方形 panel（31812），等待 Motion.Join("Start")、WaitTime(1500)，最後呼叫 scene 方法 27193。證據：AIN:1165084–1165128、1165458–1165482、1165570–1165762、1165764–1165890。

因此「滑鼠點擊後畫面灰黑，stack 還含 SceneTitle@Run」至少有兩種可能：按鈕未正確派發，或 **Start 已派發但卡在 TitleClose／Join**。不能只靠外層 SceneTitle 名称判定前者。下一輪最有區辨力的是記錄 36741／31808／31811／31812／27193 是否進入、是否返回，再與 target parts 和 queue identity 對上。

## 2. 事件完整路徑

1. `handle_events` 抽 SDL_MOUSEBUTTONDOWN/UP → `mouse_event` 修改 `key_state`。實體事件使用 50 ms hold；test sequence 直接設定 game-coordinate override 並保持 100 ms。W0/src/input.c:260–286、729–739、757–776。
2. `PE_UpdateInputState` 取得 `key_is_down(VK_LBUTTON)` 和 `mouse_get_pos`，先前後 Z 順序更新 hover／狀態；v14 再獨立搜尋 clickable target。W0/src/parts/input.c:172–186、258–291。
3. v14 命中 target 時設 `clicked_parts` 並入列 type=4，payload=(gameX,gameY,1)、parts_no／delegate_index／unique_id；未命中時入列 parts_no=0 whole-screen click，且直接 `global_set(2,1)`。W0/src/parts/input.c:273–286。
4. AIN Update(8730) 反覆讀 GetMessageType；CallDelegate(8724) 以 parts_no==0 選 wholeFunctionSet，否則驗證 delegate index 範圍與 uniqueID。type=4 選 CallFunctionMouseClick(8697)，第三參數 keyCode==1 才走左鍵事件。AIN:498024–498163、498482–498512、496758–496878。
5. CallEvent3(24812) **先取得三個 payload 參數、再 PopMessage、最後執行 delegate**；title 左鍵 delegate 36741 呼叫 Start。AIN:914625–914659、1165084–1165128。
6. WaitForClick(9196) 更新畫面後讀 GetClickNumber；正 parts number、busyLoop 等條件可退出。BeginInput/EndInput 目前只是開關單一 bool，EndInput 清 clicked_parts。AIN:525123–525144、525172–525279；W0/src/parts/input.c:373–396。

## 3. 已核實並實作的 queue 修復

### Seek 提早 Pop 與 getters 使用不同訊息

全 AIN 只有 **一個直接 SeekMessage callsite**：CPartsMessageManager.Update#1(8731)，AIN:498521。它呼叫 Seek 後立刻讀 GetMessageType／GetMessagePartsNumber，然後照一般流程派發。原本 W0 的 Seek 在找到目標時已將 head 移到下一則，而 metadata getters 又讀新 head，造成目標被跳過。

目前可見的 UpdateMessage#1 caller 在 editor 的 CPropertyCore.TermParts(2939)，AIN:211402、211406；尚未證明標題有進入此路徑。因此這是確定契約缺陷，**不是已確認的此次 title 故障根因**。Rufim 全 src 未見 SeekMessage 實作；其名字只作為新 message API 的存在判斷，沒有可直接搬用的 Seek 解法。

修後 `W/src/hll/pe_v14_message.c:125–133`：仍保留原本略過前方不匹配訊息的策略，但找到匹配 head 時不移除，交由一般 Pop 消耗。返回型別也改為 `void`，符合 libraries.txt:527 的 `void SeekMessage(int)`。對「略過的非匹配訊息應如何保留」沒有新增假設；本補丁只修提前消耗目標的確定問題。

### 所有 CallEventN 都先讀 payload 再 Pop

核實所有直接 PopMessage 及 GetMessageVariable 的呼叫點：CallEvent1<int/bool>、CallEvent2、CallEvent3、CallEvent5 均在 Pop 前把參數複製進 local，Pop 後才呼叫 delegate。AIN:914555–914743；零參數 CallEvent0 直接 Pop（498353–498368）。

原 W0 的 type／parts getter 讀 head，variable getter 卻優先讀 `msg_current`（上一則 popped message），直到 Release 清除。當同一批或巢狀更新中有第二則訊息，可能把第二則事件的 identity 與第一則 payload 混在一起。首個孤立點擊不一定遇到此缺陷，不能直接認定每次新遊戲都被此問題阻塞。

修後移除 msg_current，metadata／int／float／bool／count 一律以同一 head 讀取。Pop 只移除一則；Pop 後讀 vars 應取得**下一則** payload，空佇列則零值。這才符合 AIN；沒有保留「Pop 後繼續讀上一則」的錯誤註解契約。

### Release 一起審核

全 AIN 唯一有效 ReleaseMessage 呼叫是 Update(8730) 讀到 type=-1 後收尾（498512）；wrapper 本身在526110–526115。它沒有每處理一則就 Release，所以不能把 Release 等同額外 Pop。

修後 Release 令 head=tail，清掉所有待處理訊息，與原 upstream clear-queue 語意及 R 的當輪清理方向相符。R 對同 frame 延遲 click 另有保留策略（R/src/parts/message.c:290–307），W 的 ring 沒有 frame 延遲機制，未移植該整套行為。**AIN 在此處隊列已空，單靠這個 callsite不能證明非空 Release 的所有歷史語意**；full clear 是與原 helper 一致的有限實作選擇。

## 4. GetMessageVariableString 的 ABI 適配

同檔原本以 `void(int, string**)` 強制替換此 getter，但 CN 宣告是 `string(int)`（libraries.txt:535；code_gap 另直接讀取本次 AIN metadata 證明 lib27 function65）。

修後 `W/src/hll/pe_v14_message.c:156–186` 依完整宣告挑選：

- AIN_STRING return、argc=1、arg0=AIN_INT：使用新 adapter，回傳 `string_ref(&EMPTY_STRING)`。
- AIN_VOID return、argc=2、INT+REF_STRING：保留原 out-string adapter。
- 不符合這兩種宣告者不猜測替換。

此佇列仍沒有字串 payload；只是令既有空字串行為具有正確 ABI 與 reference 所有權。沒有宣稱它是 ADV 字不見的根因，也沒有改變文字渲染／CG 儲存。

## 5. 局部 regression 與低量埋點

重跑：`python3 <WORKSPACE>/work/stage2/queue-tests/run.py`。

fixture 直接包含 production pe_v14_message.c，連結既有 libsys4 和真 libffi。驗證实际 queue 操作序列、兩種字串 ABI 的呼叫與 reference 平衡；不用完整引擎／遊戲。

| 版本 | 結果 |
| --- | --- |
| 原 stage1 WIP | 345 checks，120 failures，exit 1（預期） |
| 修補後 | 347 checks，0 failures，exit 0 |
| 修補後 fixture ASan／UBSan | 347 checks，0 failures，無 sanitizer 診斷 |

前後 checks 相差 2 是因原碼已辨識成錯誤新 ABI，fixture 不刻意以錯簽名呼叫造成 crash，而跳過其兩項實際調用檢查。修後兩項才執行。

覆盖：一般 Peek→vars→Pop、Pop 後新 head、巢狀 dispatch 先複製參數、Seek→type/parts/identity→vars→Pop、重複 Seek 不移除、Release 清多則且不 double-pop、空 queue、不存在 target、ring wrap；string return/out/未知宣告。`git diff --check` 通過。

root 要求的 `XSYS4_STAGE2_TRACE` Text／CGName／Active setter 埋點已加入同檔：每個 setter 最多記錄前12次呼叫的 before/after，欄位包括 parts_no、字串bytes、active、current state/type、DEFAULT type、global show/alpha/pos。不輸出完整文字，不改渲染語意。用它確認同一 parts 是否先成為 TEXT，再因 CG setter 改成 CG。

## 6. 尚未修改的 click 差異與實測判定條件

| 靜態發現 | 下一輪需要的值／可證明的條件 |
| --- | --- |
| 實體 DOWN 的 50ms deadline 到期即強制 key=false，不查是否已收到真 UP。auto hook保留100ms | 同一事件的 SDL t、UP t、每次 PE_UpdateInputState t、cur/prev。如果 DOWN 至首次 parts 取樣已過50ms，這次click會遺失；若有取到上升沿，不能再用此假說解釋 target錯誤。長按超50ms也會被合成放開。 |
| 實體 input 使用現在的 SDL_GetMouseState 座標，而不是button event當下x/y；override直接回傳指定game座標且不自動解除 | 同 frame 的event/raw polled/override/final gameXY，加window/drawable/logical/viewport。不預設比例2；也要查事件後是否游標已移動。 |
| hover 用DEFAULT hitbox；v14 click第二輪用已切換後的state hitbox | 同一part的DEFAULT／HOVER／CLICKED尺寸、surface_area、type、state、hit結果。parts_set_state會回退未初始化state（W0/parts.c:917–925），但已初始化而尺寸不同仍可導致兩次命中不同。 |
| hittest僅矩形，local pos加parent global pos；沒有套用縮放／旋轉／像素alpha | W0/parts/input.c:50–58；parts.c:405–424。動過scale/rotation的畫面與hitbox可不同；需當次part transform與實際邊界，不能只憑可見字的位置判斷hit。 |
| hover/click capture前段與v14 target後段過濾條件不同，後段只選clickable、show、alpha>0、!pass_cursor且no<1000001000 | 記錄符合幾何但被哪個flag排除、最終target／clicked_parts。非clickable但!pass_cursor的遮罩在前段可consume，後段卻略過；高ID範圍也被直接略過。 |
| 未命中target就whole-screen click並直接設busyLoop | 若target=0且global2在此變1，WaitForClick可能以0退出；若36741已進入則優先查TitleClose/Join，不能再說沒派發。 |
| Begin/EndInput是單一bool，Begin不清前次clicked_parts；巢狀callback開新等待可繼承舊值，內層End可關掉外層 | 記錄Begin/End時活躍9196 stack數、clicked_parts與parts_began_click。R有depth／live-frame補正與Begin清除（R/parts/input.c:1180–1253），但不應盲移整套。 |
| W0 parts_msg_push對v14直接return，ring只由專門click路徑寫入 | W0/parts/message.c:43–51、全src的parts_enqueue_message呼叫；hover/key/down/up/drag等訊息目前不會走ring。一般標題左鍵type4本身正確，其他事件缺失須另驗，不要把type改回5。 |
| controller主要用於排序／配置，v14 click迴圈沒有檢查active controller；SetEnableInput為no-op、IsEnableInput恆true | 只有當trace顯示非當前controller的part奪取input，或腳本確實關閉input仍派發，才能定位為當次原因。InputDisabler也會建立高z矩形，不能只查SetEnableInput。 |

source 的 message mapping 舊註解亦已修正：AIN switch值4確實是MouseClick，5是MouseDoubleClick，兩者均經三參數CallEvent3；keyCode 1／2／4分左／右／中鍵。原註解「1=button、5=left」不可靠。

## 7. 交接範圍

本項交付的 queue／ABI 檔案為 `W/src/hll/pe_v14_message.c`；完成後 message-window setter 區交給 code_gap_review，該agent接續負責文本儲存，並保留低量trace。本項後續另修改 `W/src/input.c` 的 pending-release，見第9節；不改 parts/input、Array 或 VM。先前 ffi context補丁已交付；同團隊 upstream_activity 經協調獨立負責 Array.Erase selector 及 ffi linkage 小hook。

本次局部測試只驗證已定位的 C 契約，**未證明手動新遊戲、對話停止、中文顯示或完整遊戲 sanitizer 已通過**。完整實測結果以 root 接續的固定版本 run 為準。

## 8. 50 ms deadline 的有界複核（修復前的判斷）

**靜態上確定會提前放開長按；目前沒有 runtime 證據证明本輪 click 因此遺失。** W0/src/input.c:270–275 的 DOWN 設 `mouse_hold_until=t+50`，UP 若早於期限就暫不清 key；但 :757–765 的到期處理不檢查有沒有收到 UP，一律清 key。因此原本想延後「已發生的短按放開」，實際卻也截斷「尚未放開的長按」。

| 時序（假設沒有其他key寫入） | 現行結果 |
| --- | --- |
| t=0 同一pump收DOWN+UP；t=10 parts取樣；t=50後再pump | parts可看到一次true上升沿，之後放開。這是原機制想保護的短按。 |
| t=0 DOWN；t=10 parts取樣；使用者持續按住；t=50後pump | key被提前清false，拖曳／長按語意錯；但已設為正值的clicked_parts不會因此自動歸零。 |
| t=0 DOWN；在任何parts取樣true之前，t≥50某次pump先清key；之後parts取樣 | cur一直false，這次按下邊緣可能完全遺失。**必要條件是到期處理已執行，不只是wall clock已過50ms。** |
| parts_began_click=false時取到DOWN，把prev_clicking設true；其後BeginInput才打開 | 新等待即使仍看見cur=true，也沒有新的false→true邊緣；這是輸入session時序的另一個原因，不是deadline本身。 |

`key_is_down` 只是回傳key_state（W0/src/input.c:184–189）；`mouse_get_pos` 的 SDL_PumpEvents 不等於 handle_events 對queued button的處理。InputDevice 的查詢和 VM／UpdateComponent／UpdateView 等位置可呼叫 handle_events；兩次parts取樣之間可能有多次pump。所以實測應記錄「哪一次expire branch清了key」，不能只比較滑鼠事件與下一frame的時間差。

**是否能造成 WaitForClick 持續讀0：** 若上升沿完全錯過、BeginInput尚未生效、hit-test沒有target，確實可能沒有正 clicked_parts。反之，只要這次PE_UpdateInputState已設 clicked_parts>0，deadline沒有寫該欄位；正常持續讀0還要查 EndInput清除、巢狀等待、或讀取的場景／時點。將50ms改大不能修 queue、receiver、TitleClose卡住或coordinate override。

最小候選修法（後續已依root授權實作，見第9節）：每個滑鼠button另記 `pending_release`；DOWN設deadline並清pending；早到UP設pending；晚到UP立即放開並清deadline；expire只放開已收到UP的pending button。時間比較應用SDL ticks可處理回繞的判斷。這保留短按至少50ms，同時長按等真正UP才放開；如果需要保證任意慢frame也不漏click，還需consume-on-sample的edge latch，那是另一個範圍。

既有auto sequence獨立用100ms按住、不設定mouse_hold_until；override只固定座標，不代表按鍵一直按住。測試中不要混入先前實體deadline與auto click：若混用，auto branch先设true、随后已過期的實體deadline又可在同次handle_events清false。純單次auto與全新process的實體click各自量測才可比較。

修復的最小驗收為：同pump短按仍可被讀取；真長按超過200ms持續true直到UP；新DOWN不被先前pending UP清掉；不改變純auto sequence100ms分支；有／無override時僅座標路徑不同。以上為授權修復前的審核判斷，沒有將deadline宣稱為自走根因。

## 9. 後續授權的 pending-release 修復與驗證

`W/src/input.c:264–316` 新增每button的 `mouse_release_pending`，DOWN重新開始期限並清除舊pending；只有已收到UP且尚未過50ms才pending。晚到UP立即釋放；`release_pending_mouse_buttons` 只清pending，不會在尚未UP時截斷長按。handle_events在原位置呼叫此helper，沒有重排SDL事件或auto sequence。

採用 `SDL_TICKS_PASSED`，支援32-bit ticks回繞，包含deadline恰好為0的情形。UP沒有對應的DOWN且key本來為false時保持false。unsupported button經既有sdl_to_sact_button回0，不進入按鍵／deadline陣列修改。

root既有mouse trace保留，另外令 `XSYS4_STAGE2_TRACE=0` 或空字串停用mouse trace；正常啟用時仍限制40個事件。沒有修改coordinate override、純auto sequence的100ms分支或parts click判斷。

重跑：`python3 <WORKSPACE>/work/stage2/input-tests/run.py`。

| 版本 | 結果 |
| --- | --- |
| 修改前，含root原有mouse trace | 56 checks，21 failures，exit1（預期） |
| 修復後 | 56 checks，0 failures，exit0 |
| 修復後 fixture ASan／UBSan | 56 checks，0 failures，無sanitizer診斷 |

fixture編譯真正production input.c的mouse_event、key／position讀取及新expiry helper，以假SDLclock與事件服務提供確定時間，沒有SDL_Init、視窗或遊戲。修改前的expiry原本inline在handle_events，因此run.py從保存的input-before.c擷取**原始block**包成可呼叫入口，沒有重新模擬該演算法。

覆蓋left/right/middle的同pump DOWN+UP、期限前／期限到達、長按250ms、晚到UP、新DOWN取消舊pending、tick回繞、deadline0、無DOWN的UP；unsupported0/4/7/8/255；trace=0／空字串關閉與40事件上限；正常座標與既有override不變。`git diff --check -- src/input.c` 通過。

這修復了已確認的長按語意缺陷；**不保證任意慢frame的短按edge不遺失，也不證明其為本輪WaitForClick讀0、灰轉場或對話自走根因**。仍須由root對同一實際點擊的cur/prev、parts_began_click、target、delegate及TitleClose函式路徑作實測驗收。
