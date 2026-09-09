# 證據保存規則

stage1 / stage2 是本次研究與有限實測的文字附件，保留每輪 run.json、摘要、效能窗口、函式fixture與來源身份。最新失敗輪次為 stage2/runs/user-preview-20260909-120028。

公開版以 `<WORKSPACE>` / `<USER_HOME>` 代替本機路徑，MSG保留序號而省略商業遊戲台詞。未加入遊戲資產、存檔、執行檔、截圖、完整AIN反組譯或含環境的Meson testlog。原始完整證據仍在本機。`publication-manifest.json` 記錄原始與公開版本hash，不能以公開去識別化log的hash冒充原始hash。

程式碼fixture／生成器為研究快照，不是已接入CI的測試套件：部分依賴當時stage1/stage2目錄、Homebrew、外部正版AIN，以及`git show HEAD`指向修補前484f4bc；直接在目前HEAD執行可能取得錯誤基準。請依REPRODUCE建立隔離環境並調整路徑。`perf`中的free-list結果屬未採用候選，應與experimental及對應實機失敗紀錄合讀。

`all-runs-stopped.json`、`final-source-identity.json` 等記錄各自產生時的狀態，不涵蓋其後新增的user-preview；最新輪次的run.json也已記錄結束。sanitizer未報錯不代表沒有page重複釋放或洩漏。
