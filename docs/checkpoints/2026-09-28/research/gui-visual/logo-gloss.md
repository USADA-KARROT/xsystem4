# 開場 LOGO 的光澤變成黃色光條（2026-09-30）

## 症狀

開場 AliceSoft 30 週年 LOGO 掃光時，xsystem4 畫出一條淡黃色的斜向光條，橫越整個白色畫面；光條停下後一直留在右下角，直到 LOGO 淡出。

Wine 原版逐幀截圖（本機 `wine-reference-shots/logo/`，不進 repo）：光澤只出現在 LOGO 的深色部分（徽章黑邊、ALICESOFT 字母、since 1989 黑條呈淡黃），白底上完全看不見，停下後也不留痕跡。

## 資料

`SceneLogo@ShowLogo` 以 `Section:Logo[X:143 904 EaseInOutExp|Time:2200 500]` 移動 `Light`。`Scene\20_Title\Title\SceneLogo.pactex`：

| 元件 | 內容 |
|---|---|
| `Light` | 矩形部件 256×256，`アルファ` 200，**`描畫フィルタ` 3（濾色）**，子部件 `LightA`、`LightB` |
| `LightA`、`LightB` | パネル 64×400，色 (255,255,128,255)，旋轉 45°，`描畫フィルタ` 0 |

濾色：白底 → 白（看不見），黑 → 淡黃，正是原版的樣子。

## 根因

1. v14 pactex loader 只認 SJIS 的 `描画フィルタ`（`95 60 89 E6 …`）；中文版的鍵是 `描畫フィルタ`（GBK `C3 E8 AE 8B A5 D5 A5 A3 A5 EB A5 BF`），`Light` 的 3 從未讀入。`加算色` 同樣只有 SJIS 鍵。
2. 濾鏡只作用在設定它的元件本身；`LightA`／`LightB` 是 0，照一般混合畫出。flat 圖層（`render_flat_item`）早已讓子圖層沿用父圖層的濾鏡，一般元件沒有。

## 修正

- loader：`描畫フィルタ`、`加算色` 加上 GBK 鍵（與其他鍵相同的 SJIS→GBK 退回）。
- `render.c`：CG 路徑（CG、パネル、數字等）用「自己的濾鏡，沒有就用最近祖先的」，只在 v14（兩個多娜多娜版本）；舊引擎沒有原版畫面佐證，維持各自的濾鏡。子元件明確設 0 也會繼承（原版語義推定：SceneLogo 的面板就是 0 並被濾色合成；是否為「整組離屏合成」未確認）。

影響面（全部 pactex）：`描畫フィルタ` 非 0 的元件共 7 個，有子元件的只有 `Light`。另外受 loader 修正影響、原本就該有濾鏡的：標題背景三層（背景Ａ 濾色、背景Ｃ 與背景Ａ 乘算）、回合結束 1 個（加算）、戰鬥背景碼頭／衚衕（濾色）；`加算色` 非 0 的 6 個（成員狀態、地圖、戰鬥、鑑賞）。

## 驗證

- LOGO：修正後逐幀與 Wine 原版一致：白底上沒有光條，光澤只在深色部分，停下後不留痕跡。黑字上最亮的光澤色：原版 (255,255,209)，修正後 (253,249,211)。
- 標題：修正前背景是紅、橘、藍的放射色塊，比原版偏紅；修正後以洋紅為主、右側白底淡色方塊，與 Wine 原版一致（背景三層的濾鏡原本沒有讀入）。
- 新模式 `logo-gloss`（LG1）。

## 第二輪（使用者回報：淡入時黑底看得到光條、警告頁淡出時白字變黃）

Wine 原版 0.1 秒逐幀（`wine-reference-shots/intro/`，不進 repo）：淡入期間整個畫面是均勻的灰，看不到光條也看不到 LOGO；警告頁淡出時所有文字一起變暗到全黑。

1. **光條有遮罩**：`LightA` 的 `アルファクリッパー` 是 `"Logo"`（徽章）、`LightB` 是 `"Alicesoft"`（文字），原版光澤只畫在這兩個部件的 alpha 內。淡入時徽章的 Scale 是 0，光條完全被遮住；掃光時光澤只在徽章與文字上。v14 loader 沒讀這個鍵（原本寫著 not yet implemented）。修正：讀 GBK（`… A5 D1 A9 60`，「ー」是 A9 60）與 SJIS 鍵，整個 activity 建完後依名稱找部件編號設 `alpha_clipper_parts_no`；`render.c` 在 clipper 有貼圖但沒有面積（寬高 0、global 倍率 0）時不畫被遮的部件，避免奇異矩陣；clipper 沒有貼圖時照舊不遮（審查：SceneWorkResult 的 IncomeBase／InfoBase 是 loader 尚未建構的構築部件，全遮會讓兩塊背景紋理消失）。全部 pactex 有 14 個非空的 `アルファクリッパー`（Footer 的 Clipper、PhaseBar、SceneTitle、SkillName、ScriptMovie、EndingMovie、StripDialog、SceneMap 的 MapClip、SceneWorkResult 等）。
2. **警告頁白字變黃**：`CParts@MulColorR::set` 等執行 `SetComponentMulColor(n, 值, MulColorG.get, MulColorB.get)`；v14 的 `GetComponentMulColorR/G/B` 是固定回 255 的替身，R、G 被後面的設定重設回 255，只有 B 降到 0。改為讀元件自己的顏色（`AddColor` 同理，未知元件回 255／0 且不建立元件）。
3. `Time:A B` 是「長度 A、延遲 B」（`Motion::TimeParam(time, delay)`），延遲期間起始值從第一幀就套用。

驗證：淡入期間黃色像素 69 → 1（剩下的是 ALICESOFT 上的黃色筆畫）；掃光時光澤只在徽章與文字上，與原版 w56–w62 一致；警告頁淡出白字變灰、黃字變暗黃，一起到全黑。`logo-gloss` 加 LG2（遮罩名稱解析）、LG3（顏色讀回、逐通道淡出到黑）。

## 未處理

- **已完成：ALICESOFT 與標題按鈕擦入**（`190c1c8`）：v14 ClipArea 的 HLL、pactex、子樹裁切及存讀已實作。原版反組譯推翻原先最近祖先／box 反矩陣的建議：應以錨點與本地倍率建立螢幕矩形，沿祖先取交集。[證據與限制](clip-area.md)。
- 光澤邊緣：有了遮罩後大致相同，原版仍略柔和。
- 文字與 flat 元件的根不套用祖先的濾鏡（與修正前相同）。
- 回合結束、戰鬥背景、`加算色` 的 6 個元件沒有逐畫面與原版比對。
