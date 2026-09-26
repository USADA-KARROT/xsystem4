> 歷史研究／測試快照；最新狀態以本 checkpoint 的 STATUS.md 為準。本機路徑已去識別化；此文件的額度與尚未推送敘述僅指記錄當時。

# 發現
- 既有原版：<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/game-workcopy/多娜多娜 一起幹壞事吧/dohnadohna.exe。
- 已存在CN與JAST dump目錄：<USER_HOME>/Claude/projects/dohna-{cn,jast}-dump。
- 2026-09-09最新O2試玩仍卡住，57.765秒內兩次8.644/5.351秒present長間隔、無MSG；不能當作已修好。
- 既有研究只完成AIN/HLL與xsystem4分析、CrossOver身份核對，尚未證實深入分析原EXE邏輯。

## 初步新材料
- Downloads/dohnadohna_工作檔案 有既有EXE dump、反組譯與HLL解析小工具；尚需驗證其來源與二進位hash，不直接執行舊patch腳本。
- EXE agent確認指定原遊戲EXE有高熵/匿名RWX sections、稀少imports、無PDB/HLL明文字串，支持受保護/打包判斷。接續只分析既有dump。
- 舊2026-07-08 Wine基準有141張圖與等待/逐次點擊紀錄；legacy參考另有來源疑慮，不混作本fork成功證據。
- CN dump標示1.01、JAST dump1.02，不能假設只差文字。

## 本輪可重跑probe
- probes/ain_surface.c連結現有pinned libsys4並直接開啟原CN AIN，不執行遊戲。完整解碼10,626,832bytes、2,040,196個靜態指令位置、99種opcode、37,742函式/1,210結構/1,779delegate；36個HLL庫共1,701個宣告，1,562種有靜態CALLHLL引用。無越界引用；非動態覆蓋、不能直接認定全部需在首個原型實作。
- 原EXE dump定位Array #56/#57不同helper，#16 predicate找第一項後只erase一次；這是原生分支反組譯支持、尚未執行原dump函式驗證。
- CN/JAST HLL文字dump完全同hash，但fno在後段偏移（例如Join lambda CN36081/JAST36075）；應以signature/name +版本hash映射。

## 邊界與交叉核對
- 原AIN有2,024個函式address=0xffffffff（NULL/介面宣告等），不是可執行body；排除後所有函式地址都落在已解碼instruction boundary，無其他未解釋錯位。
- 37組同名HLL共97宣告；35組簽名不同。這是declaration-aware bridge設計依據，不能推論35組都是既有bug。
- 原版dump兩個jump table共952格（84 Array+868 PartsEngine）全部file-backed/executable，個別分支語義僅抽查部分，不能寫952 API完成逆向。
- 原版GetButtonCGName#221/GetMessageWindowCGName#505為string(int)，研究時不泛化其他同名getter為同一參數數量。
