# CrossOver 參考版本唯讀盤點

2026-09-09。使用者表示 `/Applications/多娜多娜.app` 是正常可玩的 CrossOver 轉譯版；**本輪沒有啟動或實測它，因此「正常可玩」屬使用者提供的狀態，不能寫成我們已驗證。** 此盤點不阻塞原生 xsystem4 修復。

## 啟動鏈與設定

`多娜多娜.app` 本身只有 Info.plist、165-byte `Contents/MacOS/launch` shell script 和圖示，遊戲不封裝於 bundle。

- Bundle ID：`com.alicesoft.dohnadohna`；CFBundleVersion：`1.0`，這是 wrapper 版本，不是遊戲版本。
- 腳本明確切換至 `<USER_HOME>/Downloads/多娜多娜 一起幹壞事吧`，執行該目錄的 `dohnadohna.exe`。
- 啟動環境：`LANG=zh_CN.UTF-8`、`WINEDEBUG=-all`、`WINE_D3D_CONFIG=renderer=gl`；最後有 `&` 背景啟動。
- `/opt/homebrew/bin/wine` 是 symlink，指向 `/Applications/Wine Crossover.app/Contents/Resources/wine/bin/wine`。
- Wine Crossover 的 plist：bundle ID `org.winehq.wine-crossover.wine`，short/build version 均為 **23.7.1-1**；minimum macOS 10.15.4。Wine 執行檔的 `file` 結果為 Mach-O x86_64，遊戲 EXE 為 PE32 Windows Intel 80386。這些是靜態檔案資訊；未執行 `wine --version`，不另推測其底層 Wine 分支／commit。
- Wrapper 沒有明訂 `WINEPREFIX`、bottle 名称或額外 DLL override。未為追查預設值而掃描其他目錄，也未讀取／修改任何 Wine prefix 或玩家存檔。`renderer=gl` 是要求的設定，不等於本輪已量測實際使用的 backend。

## 與 Stage 2 遊戲身分核對

逐位元 SHA-256 比較 wrapper 明確指向的遊戲目錄與 `work/stage2/game`：

| 檔案 | bytes | 兩側結果 | SHA-256 |
|---|---:|---|---|
| dohnadohna.ain | 3,256,218 | 相同 | `beefa6677237424a9e6dfb304299e5d47c524fb2956e1dd80aed113133fd8947` |
| AliceStart.ini | 353 | 相同 | `c09633228076f51426f23208fab5448d3766e56665ea27f25c5db5f77add1587` |
| Version.txt | 4 | 相同，內容 1.01 | `cb5d2011975d7a70e93f7cf9d2934fc752c4f1c5013a80cd34b8d2deb5ded6b0` |
| dohnadohna.exe | 4,188,176 | 相同 | `97020c28f1e481eed5a8c28986c92fc1cbf092434780a6c6a055aff280c9377a` |

兩邊 `.ain.bak_original` 也與目前 AIN 相同；`.exe.bak_original` 與目前 EXE 相同。因此僅由備份名稱／mtime 不能推論檔案有補丁差異。兩邊 `run.sh` 也相同，命令與 app wrapper 一致，但使用腳本所在目錄作 cwd。

AliceStart.ini 的有效設定：GameName=`多娜多娜繁中版`、BootName=`dohnadohna`、CodeName=`dohnadohna.ain`、SaveFolder=`SaveData`、IniFileVersion=1、RegName 空字串。未明訂顯示尺寸。Stage 2 另有 `stage1-resolution1280.ini`／`stage1-resolution1280-single.ini`，兩者相同且額外指定 ViewWidth=1280、ViewHeight=720；這是原生測試設定，不是上述 app launcher 指定的檔案。本輪沒有核對大型 CG／音訊 archive 的全部雜湊，不宣稱整個遊戲資料夾完全相同。

## 對原生修復的用途

已確認可以把此版本作為**同一份 AIN／主要設定**的行為參考：後續若原生在等待、翻頁、文字效果、座標或存讀檔遇到難以判定的瓶頸，可在相同劇情節點記錄它的實際畫面與輸入反應，和 xsystem4 比較。正式比較仍須建立各自隔離存檔及相同顯示條件，避免把既存設定或進度差異當成 engine 行為。

這不是把商業 Windows engine 的实现直接移植為原生程式；本輪沒有反編譯、分析 EXE 程式邏輯或搬用程式碼。優先完成目前 IsExist／free-list 與原生驗收；只有出現具體語意瓶頸時才使用此參考，避免擴大主線範圍。

機器可讀證據：`crossover-identity.json`、`crossover-metadata.json`。盤點只讀上述兩個明確 bundle、launcher 指向的遊戲檔案及 Stage 2 對照檔；沒有啟動 app、Wine 或遊戲。
