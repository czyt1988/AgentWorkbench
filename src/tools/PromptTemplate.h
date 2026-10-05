#ifndef AWB_TOOLS_PROMPTTEMPLATE_H
#define AWB_TOOLS_PROMPTTEMPLATE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace awb::tools {

/// 一份提示词模板（值类型）。
///
/// 模板是**骨架**：正文里带 `{{占位变量}}`，插入时逐项填写；与收藏
/// （成稿、可直接用）是两种东西，界面分处两段，数据也不同源。
struct PromptTemplate {
    /// 来源：随包只读 / 用户目录（可手改、可备份、可进 git）。
    enum class Scope {
        Builtin,   ///< 内置（config/default_prompt_templates.json）
        User,      ///< 用户（<dataRoot>/prompt_templates/<id>.md）
    };

    QString id;           ///< 稳定标识：内置用 slug（fix-a-bug），用户用文件主名
    QString title;        ///< 显示标题
    QString category;     ///< 分类（修 Bug / 加功能 / 测试…；用户的可自定义）
    QStringList tags;     ///< 标签（过滤用）
    QString description;  ///< 一句话描述（列表副行）
    QString body;         ///< 正文骨架（Markdown，含 {{变量}}）
    Scope scope = Scope::Builtin;
};

/// 模板正文的**最小头部**（用户模板文件的前几行）：
///
/// ```
/// ---
/// title: 我的模板
/// category: 我的模板
/// tags: a, b
/// description: 一句话
/// ---
/// 正文……
/// ```
///
/// 只支持「分隔线 + `键: 值`」这一层，不实现通用 YAML——模板头是给人手改
/// 的，不是给机器 Round-trip 的；字段缺失一律有默认值，坏行跳过并记警告。
namespace PromptTemplateIo {

/// 头部字段的键名（磁盘格式的一部分，改名等于丢用户模板）。
inline constexpr auto kTitleKey = "title";
inline constexpr auto kCategoryKey = "category";
inline constexpr auto kTagsKey = "tags";
inline constexpr auto kDescriptionKey = "description";
inline constexpr auto kIdKey = "id";

/**
 * @brief 从「头部 + 正文」的模板文件文本解析出一份模板
 * @param raw 文件全文
 * @param fallbackId 头部没写 id 时用它（通常传文件主名）
 * @param scope 来源标记
 * @return 解析结果；无头部或字段全空也算合法（全部走默认值）
 */
PromptTemplate parse(const QString &raw, const QString &fallbackId,
                     PromptTemplate::Scope scope);

/**
 * @brief 把一份模板序列化回「头部 + 正文」（存为模板 / 用户改写用）
 * @param template_ 模板
 * @return 可直接写盘的全文
 */
QString serialize(const PromptTemplate &template_);

} // namespace PromptTemplateIo

/// 模板变量：纯字符串操作，不联网、不调模型。
///
/// 语法（见规划 06 §2.1）：
///   `{{name}}`         必填占位
///   `{{name:默认值}}`   带默认值
/// 内置变量（自动求值，不打扰用户）：`workspace`、`date`；同名多次出现
/// 一次填写全部替换。
namespace PromptVariables {

/// 一个占位变量的形状。
struct Variable {
    QString name;          ///< 变量名（ASCII 标识符）
    QString defaultValue;  ///< 默认值（`{{name:默认}}`）；无默认为空串
};

/// 内置变量的名字（求值在 PromptVariables::apply 里做）。
inline constexpr auto kWorkspace = "workspace";
inline constexpr auto kDate = "date";

/**
 * @brief 按出现顺序提取正文里的占位变量（同名只取第一个的默认值）
 * @param body 模板正文
 * @return 变量清单（含内置变量——调用方决定是否把它们从表单里隐去）
 */
QList<Variable> extract(const QString &body);

/**
 * @brief 内置变量当前应填入的值
 * @param name 变量名
 * @param workspace 当前工作区绝对路径（空串 = 未绑定）
 * @return 可用的值；不是内置变量（或取不到）返回空串
 */
QString builtInValue(const QString &name, const QString &workspace);

/**
 * @brief 是否内置变量（表单里不出现、插入时自动求值）
 */
bool isBuiltIn(const QString &name);

/**
 * @brief 把填好的值套进正文
 * @param body 模板正文
 * @param values 变量名 → 值（含内置变量的求值结果）
 * @return 套完的正文：给了值的换成纯文本，没给值的**原样保留** `{{name}}`
 *
 * 同名多次出现全部替换；`{{name:默认}}` 在有值或有默认时替换为
 * 「值非空取值，否则取默认」，两者都空则原样保留。
 */
QString apply(const QString &body, const QVariantMap &values);

} // namespace PromptVariables

} // namespace awb::tools

#endif // AWB_TOOLS_PROMPTTEMPLATE_H
