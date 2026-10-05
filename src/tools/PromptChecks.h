#ifndef AWB_TOOLS_PROMPTCHECKS_H
#define AWB_TOOLS_PROMPTCHECKS_H

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace awb::tools {

/// 一条体检命中：规则、严重度、区间与可展示建议。
struct PromptCheck {
    QString ruleId;    ///< 稳定规则 id（开关按它记）
    int severity = 0;  ///< 0 = 提示，1 = 提醒，2 = 警告（越高越要改）
    int start = 0;     ///< 命中区间起点（文档偏移；-1 = 无可定位区间）
    int length = 0;    ///< 命中区间长度
    QString message;   ///< 建议文案（英文源串，界面层翻译）

    /// 逐字段相等（QCOMPARE 需要它；纯函数的确定性测试也靠它）。
    bool operator==(const PromptCheck &other) const
    {
        return ruleId == other.ruleId
            && severity == other.severity
            && start == other.start
            && length == other.length
            && message == other.message;
    }
    bool operator!=(const PromptCheck &other) const
    {
        return !(*this == other);
    }
};

/// 提示词体检：纯规则、纯函数（B9）。
///
/// 输入一段草稿文本 → 一组命中。无 IO、无网络、不调模型；同输入同输出。
/// 规则集见规划 06 §三；「可单独关闭」由调用方按 ruleId 过滤（开关存
/// tools.json 的 checks 段），这里不管开关——纯层只管判定。
///
/// 建议文案是英文源串（可翻译），由提示词的**质量语义**决定，不属于
/// 某个 QObject 的 tr() 上下文——界面层用 QCoreApplication::translate
/// 带类上下文包它们。
namespace PromptChecks {

/// 全部规则 id（稳定顺序 = 文档表顺序；界面清单与默认开关顺序用它）。
QStringList ruleIds();

/// 规则的显示名（英文源串；「必须先有 xx」清单用）。
QString ruleTitle(const QString &ruleId);

/// 跑全部规则。输入文本即草稿正文；空文本得到空清单。
QList<PromptCheck> run(const QString &text);

} // namespace PromptChecks

/// 上下文预算估算（C4 / E6 共用，纯函数）。
///
/// 公式是草案、必须标注为估算：ASCII 字符 / 4 + 非 ASCII × 0.9——只给
/// 量级感，不承诺与任何分词器一致。分级按上下文窗口占比（安全 < 25%，
/// 提醒 25–60%，超限 > 60%），窗口基数来自调用方（tools.json 的
/// editor.contextWindowTokens，默认 32k）。
namespace ContextBudget {

/// 估算 token 数（同输入同输出）。
int estimateTokens(const QString &text);

/// 分级：0 = 安全，1 = 提醒，2 = 超限。
int level(int tokens, int contextWindowTokens);

} // namespace ContextBudget

} // namespace awb::tools

#endif // AWB_TOOLS_PROMPTCHECKS_H
