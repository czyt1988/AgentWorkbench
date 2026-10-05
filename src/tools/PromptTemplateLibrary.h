#ifndef AWB_TOOLS_PROMPTTEMPLATELIBRARY_H
#define AWB_TOOLS_PROMPTTEMPLATELIBRARY_H

#include "core/OpResult.h"
#include "tools/PromptTemplate.h"

#include <QList>
#include <QObject>
#include <QString>

namespace awb::tools {

/// 模板库：三层来源的合并、检索与写回。
///
/// 来源（优先级从低到高，用户覆盖内置）：
/// 1. 内置：`:/config/default_prompt_templates.json`（随包只读，与
///    default_agents.json 同一套「内置定义来自数据文件」惯例——不在 C++
///    里硬编码模板条目）；
/// 2. 用户：`<dataRoot>/prompt_templates/<id>.md`（一个模板一个文件，
///    最小头部，可手改、可备份、可进 git）。用户文件与内置模板同 id
///    （头部 `id:` 或文件主名）时**覆盖**内置——与 agents.json 的
///    「用户改动优先」同一心智。
///
/// 工作区级共享模板是 P2，本版不读。
///
/// 磁盘只在构造（load）与 save/remove 时碰：列表是内存快照，用户在
/// 目录里手改文件后调 refresh() 重建。
class PromptTemplateLibrary
{
public:
    explicit PromptTemplateLibrary(const QString &dataRoot);

    /// 合并后的全部模板（内置在前、用户在后，用户覆盖的顶掉内置原位）。
    QList<PromptTemplate> templates() const { return m_templates; }

    /// 检索：关键词匹配标题/描述/标签/正文（不区分大小写，全文命中），
    /// tag 非空时同时要求带该标签——两者是与关系。数量级几十到几百条，
    /// 纯内存过滤，不上索引。
    QList<PromptTemplate> filtered(const QString &search,
                                   const QString &tag) const;

    /// 按 id 取一份模板；找不到返回空 id 的模板。
    PromptTemplate byId(const QString &id) const;

    /// 存一份用户模板（覆盖同 id 的既有文件）并刷新内存视图。
    core::OpResult save(const PromptTemplate &template_);

    /// 删除一份用户模板（内置的删不掉——它们随包再生，删除的语义是
    /// 「我的目录里那份没了」，内置条目回到随包形态）。
    core::OpResult remove(const QString &id);

    /// 重建内存视图（构造时自动跑一次；目录被外部改动后手动调）。
    void refresh();

    /// 用户模板目录（存盘时按需创建）。
    QString userDirectory() const;

private:
    void loadBuiltin();
    void loadUser();

    QString m_dataRoot;             ///< 数据根；用户目录由它拼出
    QList<PromptTemplate> m_templates;  ///< 合并视图
};

} // namespace awb::tools

#endif // AWB_TOOLS_PROMPTTEMPLATELIBRARY_H
