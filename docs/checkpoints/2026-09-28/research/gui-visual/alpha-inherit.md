# 春銷字幕退場：繼承 AlphaClipper

2026-10-05；修正前基準 `d306cd8`，libsys4 固定 `247f544`。程式 `137cbd72e49f227015a19818a12bed49ce9714aa` 已推送並雙源核對。文件隨本報告所在 commit。

## 問題與原版規則

春銷配對畫面中央仍留著環節字幕。AIN 的 PhaseBar 沒有用 Show(false) 或 Alpha=0 隱藏字元：退場是把 Base.AlphaClipper 指向另一個元件，讓遮罩移到畫面外。移植的共用繪圖入口只讀被繪元件自身的遮罩，因此 Base 的子文字沒有受到遮罩影響。

原版 `0x53546f` 呼 `0x537900` 查本地遮罩，`0x535482` 先複製父元件累積參數；`0x579ceb` 判斷本地候選，非空才在 `0x579d24` 覆蓋父遮罩，`0x535b57–0x535bf4` 再傳給子孫。語義是**最近有效遮罩覆蓋祖先**，不是所有祖先遮罩相乘。

候選須 ID 非零、找得到元件且該元件的編輯顯示旗標（+0xac）為真。動態 Show（+0xab）為 false 不使遮罩失效。選定之後 `0x537a10` 才取渲染物件；即使沒有 texture，也已覆蓋父遮罩，不回頭改選祖先。無 texture 的原版 `0x46a260` 回 false，與既有不啟用 mask 的分支方向一致。

本組只在 v14 共用 `parts_render_texture` 加入祖先解析，不改 shader、取樣座標、setter／getter 或存檔格式。v13 保留本地遮罩。既有 shader 的完整旋轉、反相、零倍率邊界等未在本組重新證明。

## AIN 宣告與呼叫鏈

| HLL | 宣告數 | 靜態 CALLHLL arg3／次數 | 呼叫者 |
| --- | ---: | --- | --- |
| SetComponentAlphaClipper(int,int) | 1 | 0／2 | f8363、f14130 |
| GetComponentAlphaClipper(int)→int | 1 | 0／3 | f8364、f14128、f14129 |

原版 setter `0x58e570` 保存本地 +0x120；getter `0x58e630` 只回本地值，不回繼承值。現有 getter stub 未在本組擴修。

- SceneWork f32483／f32485 建立 PhaseBar，先 FadeIn(true)，等待 2000 或 3000 ms 後 f36925 呼 FadeIn(false)，再進顧客預覽及配對。
- PhaseBar f31128 的退場：f31129 EnableClipper(true)，Base.AlphaClipper 指向 Clipper，Clipper X 從 -256 移至 1280、長度 300 ms；沒有隱藏或釋放文字。
- AnimateText f30928／f30930 建立字元並掛至其父 Rect，父 Rect 掛至外部 UC。最終 GUI 用實際畫面檢查後代字元能否被擦掉。
- 基準診斷 f31128(false) 已在約 28.566 秒執行，配對時的殘字不能解釋成腳本還沒叫退場。

## 卡片正文點擊的更正

上一組把「正文左鍵無反應」列為待查，這次已釐清它不是合法的左鍵切換入口。PlayerShopView f32612 只對 ButtonLeft／Right 註冊 MouseLClick，lambda f36970／36971 呼集合 Move(-1/+1)。PlayerWorkerView f32628 沒有正文左鍵事件；集合 f32650 註冊的是 MouseWheel。

暫時 trace 證實元件 900914 的祖先是 WorkerViewShort → PlayerWorkerViewCollection → PlayerShopView，與 PhaseBar 無關。正文命中它不能證明字幕擋住操作。基準與最終診斷在 (233,591)／(151,591) 分別命中箭頭 900903／900902，沒有 blocked_by。最終 t72 可見第二張人材移至前方，t78 切回第一張。未修改輸入判定或游標穿透旗標；滾輪路徑本組未實機操作。

## 新探針及獨立審查

`alpha-inherit` 三案例：

1. 真 AIN HLL setter、三層繼承、最近候選覆蓋、本地清除、未知號碼、動態 Show=false、編輯隱藏；查詢不得改本地屬性或建立未知元件。
2. 動態重掛父元件、脫離、遮罩刪除／重建、有效但無 texture 仍覆蓋、自身遮罩不造成遞迴。
3. v13 不繼承且本地未知 ID 行為不變。

探針透過 dlsym 使用正式 resolver；修正前使用原 renderer 只讀本地欄位的對照路徑。沒有 GL、素材或遊戲檔寫入；真正繪圖以 GUI 補證。native 獨立審查無 High／Medium 必修事項，條件是完成正式 GUI。

## 正式驗證

| 項目 | 結果 |
| --- | --- |
| 預設／GBK verify-step | 各61模式 VERDICT PASS、sanitizer 0；deleted-event仍預期exit87 |
| 修正前 | 正式before-check d306cd8：AC1／AC2 FAIL，AC3 PASS，exit1、sanitizer0 |
| 還原修正後 | src/include dirty0；AC1–3全PASS，exit0、sanitizer0 |
| 正常GUI | 150.285秒、時限停止、exit0、MSG88、assert／overflow0、40PNG；MSG逐位元組同上一組 |
| 目標GUI | 85.333秒、MSG88、assert／overflow0、112PNG；字幕正常顯示後退場，左右箭頭切換並切回 |
| SJIS | baseline/default/GBK的SJIS行逐位元組相同 |
| 審查 | 獨立native審查無High／Medium必修；正式GUI條件已滿足 |
| 推送核對 | 本機、ls-remote、GitHub branches API均137cbd72e49f227015a19818a12bed49ce9714aa |

[預設結果](alpha-inherit-verify-default.txt) · [GBK結果](alpha-inherit-verify-gbk.txt) · [修前修後](alpha-inherit-before-after.txt) · [GUI摘要](alpha-inherit-gui-summary.json)。完整verify在提交前執行，摘要HEAD為d306cd8；已用五個程式／fixture／設定檔SHA256 manifest確認受測檔案與提交一致。程式提交後再跑before-check並還原驗證。


### 畫面取樣與尚存差異

最終目標 GUI `codex-working-overlay-final-target`：85.333 秒、時限停止、exit 0、MSG 88、overflow 0、無 assertion，112 張 framebuffer。t01（名目27.5秒）字幕仍正常顯示於粉紅橫條；t60／t64／t68（57／59／61秒）字幕已退場。不是把整段字幕永久隱藏。

比對修正前同階段與既有 Wine 原版 f0040：兩塊露出的白色字幕區域，修前白色像素為549／829，修後三張影格均0／0、原版亦0／0；詳細座標／影格hash見[量測摘要](alpha-inherit-visual.json)。PNG僅保留本機，未提交。此處比較字幕退場結果，沒有重啟Wine，也沒有宣稱動畫毫秒時序、隨機人物或整張畫面逐像素一致。

仍未修：計數標籤、灰底、剩餘時間文字及量表灰底、部分字樣／指示點。完整春銷結算、一般讀檔system.Reset與長期穩定性未驗證。建議下一組定位計數標籤與灰底的元件來源；本組完成後等使用者GO。
