# 對白逐字淡入：未完成驗收的本機研究

本目錄只發布研究摘要及測試結果，**不包含修補程式，也不代表此功能已進入遠端分支**。最後完成的程式是 f6b6c0f；工作樹第四版基底 a2bf9b9，後續5b0815c僅更新文件。遠端harness仍91模式，本目錄92模式結果來自未提交工作樹。

## 根因與修補範圍

舊FixMessageWindowText為空實作、IsFixed恆true，字元沒有逐字淡入狀態。第四版加入字速、真AIN GetMessageSpeedRate、每字200ms淡入、time標記、Fix／IsFixed與完成後NEXT連動。相同文字內容及MsgNum不重啟；僅接受已知v14宣告，未知形狀與v13保留原綁定。runtime註冊必須自己依實際AIN宣告選函式，static_library_register不會再經全域selector。

原版靜態依據：Fix 0x5956b0、IsFixed 0x5956f0、字速乘率0x4f21a3、alpha公式0x5bd205..297、換行0x5bc785..7d6、相同內容及ID 0x4f2a60、time push/pop 0x5c0a39/0x5c0a6b。AIN直接呼叫SetText4、Fix1、IsFixed1、SetSpeed1、GetSpeed2。字元alpha乘所屬parts的global alpha。

## 已有證據

- 預設／GBK各92模式PASS，新dialogue-fade十案全過、sanitizer 0。兩份verify摘要如檔；deleted-event的預期exit87不是失敗。
- 十案包含正式HLL呼叫、missing/Fix/IsFixed、自然完成、相同ID、time巢狀、真AIN字速倍率、renderer framebuffer alpha、先render後update、v13、宣告形狀、GBK雙byte與負time。
- 最初五案before在a2bf9b9全部失敗且san0；擴充後十案正式before尚未跑。見before-after.txt，不可宣稱十案修前全失敗。
- 第四版獨立審查無未解High/Medium。第三版曾因runtime綁定繞過selector而八案失敗，已修正；不能引用第三版結果為PASS。
- 四條GUI完整duration、exit0、matched空、無stack overflow。normal150／haruuri／fightr的MSG88／629／681與上一組逐位元組一致。親自查看正常對話、Day2據點、戰鬥及戰後地圖；slow20看到文字逐步出現、完成後NEXT。

## 阻擋與接手

Wine專用受保護原版兩次未取得視窗，原因未確認，本次程序已停止；啟動前後真SaveData82/82 hash一致。沒有執行解殼EXE。**尚無原版同文字、同設定的逐字/Fix/NEXT時序比對；本機GUI及靜態反組譯不能取代此驗收。**

修補與十案fixture以私人patch和11檔SHA manifest保存。下一步先核對工作樹與快照、排除Wine無視窗，補同畫面動畫對照、十案正式before及突變驗證。必要修正後重審，再走既定提交／提交後兩組verify及GUI／推送雙源核對流程。harness README的92模式與十案說明須隨正式程式一併更新。

已讀色、font/ruby styling、scaled-time與完整持久化未在本組處理。其他遊戲callback遞迴Update與極端整數溢位相容性未驗證。仍不能當作完整遊戲使用。
