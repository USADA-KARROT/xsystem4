# 音訊在視窗建立前初始化

2026-10-10。音訊主線提交：`70c7ed6`（由獨立支線 `c96f79f` 接入）。程式已推送並以 ls-remote／GitHub branches API 雙源確認；提交後兩組89模式與三條實機路線通過。

## 症狀與直接原因

使用者回報遊戲沒有音樂。主線 `f517ca8` 的私人診斷執行完整跑了 90.226 秒，正常 duration 結束、exit 0、無 VM error。開場、據點、春銷準備與春銷通用的四首 BGM 都成功找到資料，`Prepare=true`、`Play=true`，目標音量為 1.0；但沒有任何 PCM callback。

這次執行的 `SDL_OpenAudioDevice` 回傳 device 0，driver 為 NULL，原因是 `Audio subsystem is not initialized`。因此「播放 API 回成功」沒有證明裝置實際取樣；當次無聲也不是因為音樂未加入或檔案找不到。

## 初始化順序

| 階段 | 呼叫鏈 | 修正前結果 |
| --- | --- | --- |
| 函式庫模組初始化 | `init_libraries` → `KiwiSoundEngine._ModuleInit` → `audio_init` → `mixer_init` | SDL AUDIO 尚未初始化就呼叫 `SDL_OpenAudioDevice`，開啟失敗 |
| 音訊初始化返回 | `audio_init` 的一次性 guard | 即使沒有輸出裝置也設為已初始化，後續不再開啟裝置 |
| 第一次建立視窗 | `gfx_init` → `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)` | 音訊子系統此時才就緒，但先前失敗的 mixer 不會重試 |

這是 SDL 平台初始化順序缺陷。Windows 原版不使用這條 SDL 路徑；本組依 runtime 診斷、現有程式呼叫鏈與 SDL 的初始化／參照計數契約修復，不以原版函式位址推定 SDL 行為。

## 修正

只改 `src/audio_mixer.c`。先完成原有 mixer、階層、串流與 metadata 的建立，再處理 SDL 輸出：

- AUDIO 尚未初始化時，呼叫 `SDL_InitSubSystem(SDL_INIT_AUDIO)`；已初始化則沿用，避免額外取得子系統參照。不呼叫 `gfx_init`，不建立視窗。
- 子系統初始化失敗時，以 WARNING 記錄 SDL 原因並返回，保留已建立的 mixer 狀態。
- `SDL_OpenAudioDevice` 回 0 時，同樣記錄 WARNING 後返回；只有有效裝置才呼叫 `SDL_PauseAudioDevice(..., 0)` 開始取樣。

保留 `audio_init` 的一次性初始化，不新增自動重試，不改 `Prepare`／`Play`、混音增益、選曲或 AIN ABI。共用入口也供 vmMusic／vmSound 使用；SACT2／Gpx2Plus 先走 gfx 的既有順序會沿用已初始化 AUDIO。本組不新增 AIN 版本或字碼分支，不改存檔格式。

## 修前／修後證據

新模式 `audio-init` 有三案，各自 fork，使用 SDL dummy driver，不碰實體裝置或遊戲音檔：

| 案例 | 驗證 |
| --- | --- |
| AI1 | 沒有 AUDIO／VIDEO 時走真正 Kiwi 模組入口；合成 stereo stream 必須由引擎的 SDL callback 在另一執行緒持續 refill。重複初始化保留名稱、音量與串流；一次 QuitSubSystem 即清除 AUDIO |
| AI2 | 外部先初始化 AUDIO，再走同一路徑；一次 QuitSubSystem 即清除 AUDIO，確認模組沒有再加參照 |
| AI3 | 無效 driver 下音訊初始化非致命返回，留下可辨認的 SDL 警告；mixer 名稱／音量仍可使用，重複初始化不重建 |

正式 before-check 在獨立支線基底 `f517ca8` 與主線整合前 `9eb1a91` 上，各有 2／3 案失敗，rc 1、sanitizer 0：AI1 無 AUDIO／driver／callback，AI3 沒有警告；AI2 通過，作為既有初始化順序的守衛。輸出見 [before／after](before-after.txt)。修後三案全過；主線提交後 default／GBK 各89模式 PASS、san=0。

私人因果實驗只在觀測用來源副本提前初始化 AUDIO，與正式修補分開。它完整執行 90.232 秒，成功開啟 coreaudio device 2；88／88 個 PCM 觀測區間有非零輸出，穩定 BGM 區間 35／35 有非零輸出，nonfinite 最大值 0、MSG 88。這支持「缺少 SDL AUDIO 初始化直接阻止取樣」的因果判斷；該執行檔不是正式修補版本，不能代替正式版本 GUI 驗收。

## 驗證

- 音訊支線有87模式；接入G15／G16後，主線 `70c7ed6` 的 [default](verify-default.txt)／[GBK](verify-gbk.txt) 各89模式 PASS、san=0，tracked diff為空。三組新fixture均保留。
- 已審 src/include diff SHA256：`9453cc55c74fa63bc6e4e5c7e8c0dfa86efcfbb62df28369daaaf1f891b94286`。獨立支線提交為 `c96f79f`，主線提交 `70c7ed6`；production音訊來源與fixture已確認和已審支線逐位元組相同。主線只解開harness模式清單的整合衝突。
- 正式修正版提交前 GUI：normal150 150.325秒／MSG88，haruuri 182.403秒／MSG629，fightr 350.293秒／MSG681，各完整duration、exit0、error0。root看過開場、Day2及fightr t150回地圖。實際聽感及原版切曲／音量比較未驗證，詳見 [GUI 摘要](gui-summary.json)。
- 本機修前紀錄相對 `XS4_WORK` 為 `logs/before/f517ca8-audio-init.txt`。本文只整理必要數據，不附私人路徑、遊戲資產、完整 AIN 或截圖。

## 獨立審查

限定範圍的獨立唯讀審查沒有發現 High／Medium blocker，核對了 AUDIO 參照計數、共用 HLL 入口、失敗分支、一次性初始化及 fixture callback 的生命週期。審查者沒有重跑測試；此結論不等於正式 verify 或聽感驗收已通過。

## 限制與未驗證項目

- **非致命的範圍只到音訊初始化返回。** AI3 沒有呼叫 `gfx_init`；後者仍使用 AUDIO | VIDEO，初始化失敗仍走 ERROR。不能把 AI3 通過寫成「遊戲在沒有任何音訊 driver 時也一定能繼續」。
- 子系統成功但 `SDL_OpenAudioDevice` 單獨失敗的分支只有靜態審查，fixture 沒有強制重現。
- 保留一次性初始化；裝置稍後恢復時不會自動重試，也沒有新增熱插拔、熱重啟、外部宿主任意 QuitSubSystem 或多執行緒同時初始化的保證。
- 裝置不可用時，既有 Play 仍可能回成功；本組沒有改所有無裝置 WAV／BGM 操作的語義。
- dummy refill 只證明 callback 與串流生命週期；私人 coreaudio 實驗只量到引擎交出的非零 PCM。兩者都不等於使用者實際聽見、完整 BGM 解碼正確、選曲／切曲／音量／淡入淡出與原版一致。
- 後續 gfx 初始化原本仍會再取得 AUDIO 參照；現有引擎以整體 `SDL_Quit` 結束。本組沒有改這套生命週期。
- G15 struct 陣列與G16背景構築已先整合。粉紅顧客走位 getter 是另一組尚未整合的工作；本組不把它列為已修。

## 主線提交後實機與輸出取樣

| 路線 | 秒 | MSG | PNG |
| --- | ---: | ---: | ---: |
| normal150 | 150.257 | 88 | 40 |
| haruuri | 182.341 | 629 | 120 |
| fightr | 350.304 | 681 | 162 |

三條路線完整duration／exit0／error0，對白與G16基準逐位元組一致。root親自看正常開場、春銷後Day2及戰後回地圖；戰後地圖與既有Wine同場景、G16基準三欄並排，未見這次音訊改動造成畫面退化。人物數值／隨機結果與高亮時間不同，地圖左緣淡出仍是既有缺口；沒有宣稱完整逐像素或逐幀一致。本輪沒有重新啟動Wine。

由主線70c7ed6正式修補來源建立的私人觀測版執行 90.210 秒，成功開啟CoreAudio。88／88 個約1秒區間有非零輸出，55秒後穩定區間 34／34 也非零，nonfinite最大0、MSG88。觀測版只增加紀錄，沒有額外補初始化、沒有錄音或擷取麥克風。這是交給CoreAudio的PCM證據，不是正式二進位的直接量測，也不是揚聲器聽測或原版選曲／音量／淡入淡出的等價驗收。
