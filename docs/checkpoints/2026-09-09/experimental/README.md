# 未套用的 free-list 候選

候選完整修補及當時fixture保存在 [freelist-candidate](../evidence/stage2/freelist-candidate/)。它在局部配置／釋放測試通過，但 normal-final-perf 實機停在logo，並出現timer欄位指向失效或換型slot的證據，因此已回退。

`full-stage2-candidate.patch` 是相對7月9日基準484f4bc的完整候選（包含其他Stage2修補），不是可直接疊在目前HEAD的增量。**不要直接套用到目前來源。** 正式來源保留已測的回退版本；未宣稱GC、循環引用或slot生命週期已完整修復。
