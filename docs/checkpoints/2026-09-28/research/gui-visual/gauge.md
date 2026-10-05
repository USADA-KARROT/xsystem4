# v14 HGauge／VGauge：春銷計時條的型別、數值與繪製

整理日期：2026-10-05。研究與既有測試日期：2026-09-30。

**本組已完成規定的探針、正常 GUI、Wine 量表採樣比對、獨立審查及推送雙源核對。** 春銷 Start 不再因 GetHGauge 斷言停止；這不代表完整遊戲或整張畫面已與原版一致。

| 項目 | 值 |
| --- | --- |
| 修正前基準 | `1350e04` |
| libsys4 基準 | `247f544`，本組未變更 |
| 本組程式 commit | `210564013bb0728ecbb72d83fca86d62f4ca8fa4` |
| 最終遠端雙源核對 | 2026-10-05，本機／ls-remote／GitHub branches API 均為 `210564013bb0728ecbb72d83fca86d62f4ca8fa4` |

## 問題與根因

進入春銷並按 Start 時，`DecisionTimerView.jaf:19` 的 `(nonnull) m_act.GetHGauge("Gauge")` 斷言失敗。AIN 先檢查普通狀態是否為原版 HGauge 型別 22，再建立包裝物件；修正前的 pactex loader 未建立真正的量表狀態，將圖片當成一般 CG，查詢因此回空。

只更正回報型別仍會留下第二個問題。倒數與復原動畫呼叫 `CHGaugeParts.Numerator::set`；此方法先讀取原始分母，再以新分子、既有分母呼叫 `Parts_SetHGaugeRate`。修正前的分母 getter 固定回 0，底層又只保存商值 `rate`，無法保留這個契約。

本組建立 v14 HGauge／VGauge 狀態，保存原始分子、分母、反轉方向、CG 名稱及 surface rectangle，並以原版幾何規則繪製。v13 保留既有 setter／繪製路徑。

## AIN 呼叫鏈與宣告統計

以下十進位編號是 AIN 函式編號，與後文原版 PE 的虛擬位址不同。

| 階段 | 函式編號 | 行為 |
| --- | --- | --- |
| 建立計時條 | `DecisionTimerView@0` 32697 | 查詢 `GetHGauge("Gauge")` 並要求非空 |
| 預設狀態 | `CActivityWrap@GetHGauge` 601／602 | 委派至帶 state 的版本，state=1 |
| 型別檢查 | `GetHGauge#2/#3` 603／604 → `CompParts` 534 | 指定 native type 22，先驗名稱，再比較狀態型別 |
| 查詢底層型別 | `parts::detail::GetComponentType` 9147 | 呼叫 `PartsEngine.GetComponentType(number,state)` |
| 包裝量表 | `CSpriteParts@GetHGauge#1` 15136 → `CHGaugeParts@0` 11248 | 建構包裝物件，呼叫 `SetComponentType(number,22,state)` |
| 更新分子 | `CHGaugeParts@Numerator::set` 11259 | 讀分母 getter 11260，再呼叫 `SetRate` 11263 |
| 更新分母 | `CHGaugeParts@Denominator::set` 11262 | 讀分子 getter 11257，再呼叫 `SetRate` 11263 |
| VGauge 對應鏈 | CActivityWrap 605–608；CSpriteParts 15137／15138；ctor 16224 | 對應 native type 23 |

`CompParts` 只有在呼叫端要求通用低階型別 18 時才放寬比較；HGauge 要求 22，不可透過跳過斷言或偽造通用型別解決。

完整 AIN 中，以下 **18 個 HLL 名稱各只有一種宣告**，合計 **42 個靜態 CALLHLL 呼叫點**；H、V 各 21，所有 CALLHLL 的型別參數均為 0。這是呼叫點數量，不是執行次數。

| HLL 名稱 | 回傳值與 AIN 參數形狀 | 次數 | 呼叫所在 AIN FUNC |
| --- | --- | ---: | --- |
| `Parts_SetHGaugeCG` | `bool(int,string,int)` | 2 | 8537、11256 |
| `Parts_GetHGaugeCG` | `string(int,int)` | 3 | 8538、11254、11255 |
| `Parts_SetHGaugeRate` | `bool(int,float,float,int)` | 2 | 8539、11263 |
| `Parts_GetHGaugeNumerator` | `float(int,int)` | 3 | 8540、11257、11258 |
| `Parts_GetHGaugeDenominator` | `float(int,int)` | 3 | 8541、11260、11261 |
| `Parts_SetHGaugeReverse` | `void(int,bool,int)` | 1 | 11266 |
| `Parts_IsHGaugeReverse` | `bool(int,int)` | 2 | 11264、11265 |
| `Parts_SetHGaugeSurfaceArea` | `bool(int,int,int,int,int,int)` | 2 | 8542、11269 |
| `GetHGaugeSurfaceArea` | `void(int,wrap<int>,wrap<int>,wrap<int>,wrap<int>,int)` | 3 | 8543、11267、11268 |
| `Parts_SetVGaugeCG` | `bool(int,string,int)` | 2 | 8572、16232 |
| `Parts_GetVGaugeCG` | `string(int,int)` | 3 | 8573、16230、16231 |
| `Parts_SetVGaugeRate` | `bool(int,float,float,int)` | 2 | 8574、16239 |
| `Parts_GetVGaugeNumerator` | `float(int,int)` | 3 | 8575、16233、16234 |
| `Parts_GetVGaugeDenominator` | `float(int,int)` | 3 | 8576、16236、16237 |
| `Parts_SetVGaugeReverse` | `void(int,bool,int)` | 1 | 16242 |
| `Parts_IsVGaugeReverse` | `bool(int,int)` | 2 | 16240、16241 |
| `Parts_SetVGaugeSurfaceArea` | `bool(int,int,int,int,int,int)` | 2 | 8577、16245 |
| `GetVGaugeSurfaceArea` | `void(int,wrap<int>,wrap<int>,wrap<int>,wrap<int>,int)` | 3 | 8578、16243、16244 |

Surface getter 的四個 `wrap<int>` 各占兩個 VM 槽，合計 **10 個 VM 參數槽**；C ABI 是六個參數 `(int,int*,int*,int*,int*,int)`。本組新增的 12 個 getter／reverse wrapper 依完整宣告形狀選擇綁定；未知形狀保留既有綁定，不新增 `VM_ERROR`。既有 SetCG、SetRate、SetSurface 共六個匯出沿用原入口。

## 原版靜態語義與位址

下列均為本次研究使用的繁中版原版 PE image VA。不同版本執行檔的位址不可直接沿用；本報告僅記錄語義摘要，不包含反組譯傾印或遊戲檔案。

| 機制 | 原版位址 | 確認的語義 |
| --- | --- | --- |
| 低階狀態工廠 | `0x5b77f0` | 依型別與 state 建立；型別改變才重建 |
| HGauge／VGauge 型別 | `0x4e3350`／`0x52cc30` | 分別為 22／23 |
| HGauge／VGauge 建構 | `0x5a7b50`／`0x5c3f70` | 分子、分母預設 100／100，reverse=false |
| 型別名稱映射 | `0x5b8f90` | pactex 名稱映射為原版型別 |
| HGauge／VGauge loader | `0x5a8660`／`0x5c4500` | 讀取該 state 的 CG、surface、分子、分母、reverse；數值缺省 0／0 |
| HGauge／VGauge SetRate | `0x566b40`／`0x566e90` | 無 CG 仍保存原始分子、分母；setter 不做除法 |
| HGauge 分子／分母 getter | `0x598e20`／`0x598e90` | 未知元件回 0；既有元件可先轉為 HGauge 狀態再讀值 |
| SetCG | `0x5a7ca0` | 同名直接成功；成功更換圖片才改名稱，保留數值、反轉與 surface；載入失敗保留舊圖片與名稱 |
| HGauge reverse setter／getter | `0x566c60`／`0x566cc0` | 保存與讀取反轉旗標 |
| HGauge surface setter／getter | `0x566d10`／`0x566db0` | 原樣保存與讀回四個整數 |
| 有效 surface | `0x59b760` | 繪製時計算有效來源矩形 |
| HGauge／VGauge render | `0x5a7d90`／`0x5c4020` | 依有效 surface 與原始分子、分母決定裁切範圍及方向 |
| Gauge width／height | `0x5a7fc0`／`0x5a8000` | 回報有效 surface 大小，不隨進度縮小元件或子元件座標 |

建構預設 100／100 與 pactex 缺省 0／0 是兩種不同情況。Getter 遇到既有錯型別狀態時，會建立對應量表狀態；未知元件則不建立，surface 的輸出參照保持原值。越界 state 採用引擎安全檢查，未推廣原版未確認的越界行為。

有效 surface 與填滿規則：

- 寬、高都不大於 0 時，使用整張 CG，忽略原始 x/y。
- 否則 x/y 下限為 0，再裁至 CG 的右／下邊界；只有單一軸為 0 時不擴成全圖。
- 分母不大於 0，或分子等於分母時，顯示全幅。
- 其餘情況先將分子限制於 `[0, denominator]`，計算 `float(有效長度) * 分子 / 分母`，最後截斷為整數像素。
- Getter 保留未裁切的 raw rectangle／raw 數值，不能從已限制的顯示比例推回。

令有效 surface 為 `(sx,sy,w,h)`、填滿長度為 `p`：

| 方向 | 來源矩形 | 元件內顯示偏移 |
| --- | --- | --- |
| H 正向 | `(sx,sy,p,h)` | `(0,0)` |
| H 反轉 | `(sx+w-p,sy,p,h)` | `(w-p,0)` |
| V 正向 | `(sx,sy+h-p,w,p)` | `(0,h-p)` |
| V 反轉 | `(sx,sy,w,p)` | `(0,0)` |

反轉控制填滿方向，不翻轉圖片內容。負尺寸及非有限數值採安全處理，未宣稱所有極端 IEEE 浮點值與原版逐位元相同。

## 計時條與資源範圍

目標計時條圖片尺寸為 256×16，普通狀態是 HGauge 22、分子 0、分母 1、reverse=false；另外兩個狀態是空 CG 19。Recover 將分子從 0 動畫更新至 1，用時 1000ms；計時器設定為 15000ms，更新值為 `clamp(1 - elapsed_ms/15000, 0, 1)`。

`StartMatching` 的順序為 Recover → ShowReady → timer.Start。因此原版比對必須對齊相同場景階段，不能只用按下 Start 後的絕對時間當作同一幀。

Motion 的 Gauge 目標值為 23，GaugeRoot 為 24；AIN 的 `GetCurrent` 27014 與 `SetPartsValue` 27019 分別讀寫原始分子、分母，並不直接呼叫 gauge motion HLL。

資源檢查涵蓋 195 份 pactex，既有解碼對應的原始 bytes 與工作副本逐份一致。15 份含 HGauge，共 33 個 HGauge state，其中 9 個 reverse=true；未發現 VGauge state。所有量表 surface 都是零矩形，但非零 surface 語義仍由合成探針驗證。此處只有結構統計，未附任何資源內容。

## 實作範圍

| 檔案 | 變更 |
| --- | --- |
| `src/hll/pe_v14_activity.c` | 依命名狀態建立真正 H/VGauge，讀精確欄位，避免未知分支或舊 CG fallback 覆蓋 |
| `src/hll/pe_v14_gauge.h`、`PartsEngine.c` | 12 個 wrapper 與宣告形狀檢查，保留未知綁定 |
| `src/parts/parts_internal.h`、`parts.c` | 保存 raw 分子、分母、名稱與反轉；建立真正型別；surface／有效大小與填滿幾何 |
| `src/parts/render.c` | 保持完整 CG quad，平移負 surface 原點，再透過既有 shader 裁來源範圍；進度不縮小元件外框 |
| `src/parts/save.c` | v14 XPE v5 保存新增欄位及型別 metadata；舊 v3/v4 以 `(舊ratio,1)` 遷移，並先恢復 H/V 狀態型別 |
| `harness/probe/gauge_fixture.inc` | 六組使用合成圖形資料、不需 GL 的探針 |
| `harness/probe/clip_area_fixture.inc` | 版本檢查由 `==4` 改為 `>=4`，接受 v5，保留原有 ClipArea 斷言 |

v13 仍使用既有紋理更新路徑及 v3 存檔格式。CG 成功載入後釋放解碼物件；狀態清除時釋放名稱與紋理，不讓來源尺寸欄位重複擁有同一個 GL texture。

XPE v5 roundtrip 不代表一般遊戲存讀檔已完成；其他存讀檔阻礙不在本組驗證範圍。

## 六組探針

所有新符號透過 `dlsym` 取得，修正前版本可以建置探針。各 case 獨立行程執行。

| Case | 驗證內容 | 明確邊界 |
| --- | --- | --- |
| HG1 | 合成 GBK pactex：亂序命名狀態、無關 surface 分支；native 22／23 與真 H/VGauge state；raw 數值、reverse、surface、缺省值 | 不使用真 CG 或 GL |
| HG2 | 真 AIN HLL：100／100 建構預設、無紋理 SetRate、反轉、負 surface 與四個 ref 輸出、同名 CG 保值、既有錯型別轉換、未知元件不建立 | 同名 CG 使用空名稱；未知 ABI 分支另由靜態審查確認 |
| HG3 | 有效 surface、H/V 正反向、截斷、超額／負分子、零／負分母；source 四角 UV 乘正式 render matrix，32 個角點檢查 | 驗證幾何與 shader UV 契約，不執行 GPU shader |
| HG4 | 真 `CHGaugeParts.Numerator::set`／`Denominator::set` 虛擬方法派送，保留另一個 raw 值與 VM stack 平衡 | 不建立完整遊戲場景 |
| HG5 | 記憶體 XPE v5 roundtrip：H/V raw 值、reverse、surface、空 CG 名稱 | 非空 CG 重新載入尚未由此 case 涵蓋 |
| HG6 | v13 型別／無紋理 setter 行為；v14 載入舊 v3/v4 的 ratio 遷移、22／23 型別及 ClipArea 對齊 | Load 後先查型別，避免 getter 副作用掩蓋遺失 metadata |

## 獨立審查修正

未參與 engine 實作的審查者提出兩項問題，皆已修正並完成靜態複審：

1. **High：二次縮小。** 既有 shader 保持完整圖片 UV，只對來源矩形做 discard。若先把 quad 縮成半寬，再裁掉一半 UV，實際只剩四分之一。正式路徑改為完整 CG quad、平移負 surface 原點。HG3 將實際來源 UV 投影至正式 matrix，能擋下此錯誤。
2. **Medium：舊檔型別遷移不完整。** v3/v4 未保存 v14 metadata；Load 後型別為 0。原探針先呼叫 gauge getter，getter 自行補型別，掩蓋了真正 AIN 先查型別的失敗。載入時現依真 H/V state 補回 22／23；HG6 在任何 gauge getter 前先確認型別。

複審未留下 High／Medium 阻擋問題。這是指定實作版本的靜態結論，不代替正式測試與原版畫面比較。

## 驗證結果

以下兩組 summary 已讀取確認；它們記錄的是 2026-09-30 的未提交實作，base=`1350e04`、diff fingerprint=`45029069b9df`，不是最終提交 SHA。收尾前重新比對全部 12 個修改檔案的 SHA-256 與交接快照，皆相同；GUI 執行檔 SHA-256 也相同，故沿用這兩組完整結果。程式提交為 `2105640`。

| 驗證紀錄 | 已確認結果 |
| --- | --- |
| [codex-hgauge-final-v2](gauge-verify-default.txt) | 59 模式 `VERDICT PASS`；sanitizer 0；gauge 6/6 |
| [codex-hgauge-final-v2-gbk](gauge-verify-gbk.txt) | GBK 組態 59 模式 `VERDICT PASS`；sanitizer 0；gauge 6/6 |

兩組均為 deleted-event 預期 exit 87，其餘模式預期 0。這保留了 deleted-event 模式本身標示的 ownership teardown 限制，不把預期退出解讀成完整解決。

先前一般速度 GUI 的兩次紀錄都不足 150 秒：`codex-hgauge-normal150` 在 34.489 秒 exit 0、MSG 88；`codex-hgauge-normal150-retry` 在 15.328 秒被操作端停止、MSG 24。兩者不能算正式 150 秒回歸 PASS；第一次提早結束的原因尚待確認。

2026-10-05 收尾證據：

| 驗證 | 結果 |
| --- | --- |
| 修正前反證 | `before-check.sh 1350e04 gauge`：6/6 case 失敗，rc=1、sanitizer=0 |
| 還原修正版 | 腳本恢復 `2105640`，src/include 無修改；重跑 gauge 為6/6通過，rc=0、sanitizer=0 |
| 正常 GUI | `codex-hgauge-normal150-20261005`：150.396秒到時停止、exit0、MSG88、assertion0、overflow0、峰值RSS357187584 bytes；已查看framebuffer為據點教學 |
| 對白回歸 | 正常GUI的88個MSG行與前一組 `codex-input-final-g150` 完全相同 |
| SJIS回歸 | baseline／final-v2既有 `sjis-chars.txt` 逐位元組相同（99行，其中91行SJIS） |
| 目標GUI | `codex-hgauge-before-start`：37.135秒GetHGauge斷言；修正版 `codex-hgauge-after-start-v2`：100.468秒到時停止、MSG214、assertion0、overflow0，越過Start及春銷教學，計時條歸零後進入後續劇情 |
| 推送 | 推送前遠端雙源皆1350e04，推送後雙源皆2105640；submodule仍247f544 |

[探針修正前後節錄](gauge-before-after.txt) · [正常GUI摘要](gauge-gui-summary.json)。原始截圖、完整日誌與存檔留在repo外。

### Wine 計時條採樣比對

原版以專用工作副本執行，先備份存檔；正常GUI與Wine分開跑。原版走標題、新遊戲、Control快進、據點教學、春銷Start與三頁春銷教學，關閉教學後採樣23秒，共218張PNG。測試結束後核對並恢復本輪變動，81個存檔雜湊均與測試前一致。

原版PNG為1280×772，扣掉上方52px視窗裝飾；移植framebuffer為1280×720。有效彩色區的共同遊戲座標是 `(803,682)`，高度16px，最大寬256px。排除關閉教學時的縮放後，左端固定，右端向左縮短。

| 階段 | Wine原版樣本 | 移植樣本 | 彩色寬度 |
| --- | --- | --- | ---: |
| 滿幅 | 倒數f0006 | Recover t25–29 | 256px |
| 近滿 | f0009 | t110 | 251px |
| 半滿 | f0080 | t146 | 128px |
| 近零 | f0145 | t180 | 12px |
| 零 | f0152 | t184 | 0px |

原版樣本來自 `working-countdown-20261005`；移植來自 `codex-hgauge-after-start-v2`。tNN是影格序號，非秒數。滿幅來自不同動畫階段，只確認最大尺寸；移植第一張穩定倒數是254px。251／128／12px三對相同寬度的彩色ROI共4016／2048／192像素，RGB全部相同、單通道最大差0。這確認裁切後的填充沒有拉伸或二次縮小，**不代表整張畫面相同**。

| 倒數量測 | Wine | 移植 |
| --- | ---: | ---: |
| 有效樣本數 | 145 | 75 |
| 寬度斜率 | −17.062826px/s | −17.069417px/s |
| 回歸殘差RMSE | 0.3067px | 0.3005px |
| 回推全長時間 | 約15秒 | 約15秒 |

斜率差約0.039%，小於採樣與整數像素量化的辨識能力。Wine以每張擷取前後時間的中點估計，擷取耗時90–135ms；移植只有200ms的名義排程，沒有逐張精確時間。這支持約15秒的相同倒數，不能宣稱毫秒級同步或精確同幀。

Recover的稀疏採樣同樣確認由左向右填滿、約1秒：原版4個未滿幅點回推0.994秒，移植5個點回推1.026秒；短動畫的精確速度差異仍未驗證。Control快進用於抵達目標，不取代正常150秒回歸。

### 畫面差異清單

| 項目 | 原版／目前差異 | 判定與後續 |
| --- | --- | --- |
| 計時條灰底 | 原版在剩餘彩色條右側仍有灰底；移植缺少 | 已確認；父construction支援仍不完整，未在本組修正 |
| 剩餘時間標籤 | 原版有文字，移植樣本未見 | 已確認；文字／部件建立鏈待查 |
| 春銷中央字幕 | 原版Ready結束後退場，移植仍留「春銷環節」 | 已確認；退場／動畫生命週期待查 |
| 對手與顧客卡片 | 原版同階段有卡片，移植樣本缺少，相應數字也不同 | 已確認畫面差異；資料／建立／繪製哪一層造成尚未確認，不用隨機人名差異判定bug |
| 據點背景 | 原版有模糊背景，移植教學後方部分為黑底 | 已確認的既有差異，前一組樣本也可見 |

春銷倒數畫面在基準會先斷言而無法抵達，故上述新可達場景的問題不能僅憑前後截圖斷言是本次引入或既有。需另做因果定位。沒有因量表通過就宣稱春銷流程完整可玩。

## 限制與下一個卡點

- 非空 CG 名稱的保存／重新載入，以及替換失敗時保留舊圖片，目前 headless 覆蓋不足；靜態核對不得冒充全部資源生命週期已實測。
- 未知 ABI 形狀保留既有綁定已靜態核對，尚未以變造宣告探針實測。
- 沒有真遊戲 VGauge 資源樣本；VGauge 的結論限於原版靜態語義與合成幾何測試。
- 本次原版依據是繁中 GBK 版本，SJIS 對應名稱沒有日文原版 EXE 的獨立證明。
- 空名稱與特殊 CG 名稱的完整清除語義、極端浮點值、把量表當 alpha-clipper 或使用 pixel-hit-test，尚未完整驗證。現有 33 個量表皆未啟用像素判定或 clickable，也未發現量表當 clipper 的資源樣本。
- 計時條父元件的 construction 背景尚未完整支援；灰底缺漏應與量表填滿比例分開記錄。本組未完成全面 UI 修復。
- 本組最深已到倒數結束後的劇情；未繼續驗證完整春銷結果及長時間遊玩。下一組原訂訊息視窗UI；考量本次原版對照缺少對手／顧客卡片，建議先由使用者決定是否優先定位春銷畫面的資料與元件建立，再處理訊息視窗及背景效果。
