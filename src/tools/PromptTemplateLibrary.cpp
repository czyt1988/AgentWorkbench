#include "tools/PromptTemplateLibrary.h"

#include "core/JsonStore.h"
#include "core/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

namespace awb::tools {

namespace {

/// 内置模板的随包资源（只读；「数据不是代码」惯例）。
constexpr auto kBuiltinResource = ":/config/default_prompt_templates.json";

/// 模板正文里的标签分隔符（头部 `tags: a, b`）。
/// JSON 里的 tags 是数组，.md 头部是逗号分隔——两侧的解析各归各的类。

/// 一份内置模板条目（default_prompt_templates.json 的一个对象）。
PromptTemplate builtinFromJson(const QJsonObject &object)
{
    PromptTemplate template_;
    template_.scope = PromptTemplate::Scope::Builtin;
    template_.id = object.value(QStringLiteral("id")).toString();
    template_.title = object.value(QStringLiteral("title")).toString();
    template_.category =
        object.value(QStringLiteral("category")).toString();
    template_.description =
        object.value(QStringLiteral("description")).toString();
    const QJsonArray tags =
        object.value(QStringLiteral("tags")).toArray();
    for (const QJsonValue &tag : tags) {
        if (tag.isString() && !tag.toString().isEmpty()) {
            template_.tags.append(tag.toString());
        }
    }
    template_.body = object.value(QStringLiteral("body")).toString();
    return template_;
}

} // namespace

/**
 * @brief 构造模板库并装载三层来源
 *
 * @param dataRoot 应用数据根（<dataRoot>/prompt_templates/ 是用户模板目录）
 */
PromptTemplateLibrary::PromptTemplateLibrary(const QString &dataRoot)
    : m_dataRoot(dataRoot)
{
    refresh();
}

/**
 * @brief 重建内存视图：内置在前，用户按 id 覆盖
 */
void PromptTemplateLibrary::refresh()
{
    m_templates.clear();
    loadBuiltin();
    loadUser();
}

/**
 * @brief 检索与标签过滤
 *
 * 空关键词 + 空 tag = 全部（左栏的默认形态）。正文命中也算——模板
 * 数量长起来之后「那段写过评审输出格式」比标题更好记。
 */
QList<PromptTemplate> PromptTemplateLibrary::filtered(const QString &search,
                                                      const QString &tag) const
{
    QList<PromptTemplate> result;
    const QString needle = search.trimmed();
    const QString wantedTag = tag.trimmed();
    for (const PromptTemplate &template_ : m_templates) {
        if (!wantedTag.isEmpty()
            && !template_.tags.contains(wantedTag,
                                        Qt::CaseInsensitive)) {
            continue;
        }
        if (needle.isEmpty()) {
            result.append(template_);
            continue;
        }
        const bool inTitle = template_.title.contains(needle,
                                                      Qt::CaseInsensitive);
        const bool inDescription =
            template_.description.contains(needle, Qt::CaseInsensitive);
        const bool inTags = std::any_of(
            template_.tags.cbegin(), template_.tags.cend(),
            [&needle](const QString &entry) {
                return entry.contains(needle, Qt::CaseInsensitive);
            });
        const bool inBody = template_.body.contains(needle,
                                                    Qt::CaseInsensitive);
        if (inTitle || inDescription || inTags || inBody) {
            result.append(template_);
        }
    }
    return result;
}

/**
 * @brief 按 id 取模板
 */
PromptTemplate PromptTemplateLibrary::byId(const QString &id) const
{
    for (const PromptTemplate &template_ : m_templates) {
        if (template_.id == id) {
            return template_;
        }
    }
    return PromptTemplate();
}

/**
 * @brief 存一份用户模板
 *
 * 用户模板永远写用户目录（内置资源只读）——覆盖内置的方式就是在用户
 * 目录放一份同 id 的文件，不在 JSON 里打补丁。
 */
core::OpResult PromptTemplateLibrary::save(const PromptTemplate &template_)
{
    if (template_.id.isEmpty() || template_.title.isEmpty()) {
        return core::OpResult::failure(QCoreApplication::translate(
            "PromptTemplateLibrary",
            "A template needs an id and a title."));
    }
    QDir().mkpath(userDirectory());
    const QString path = userDirectory() + QLatin1Char('/')
            + template_.id + QStringLiteral(".md");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return core::OpResult::failure(QCoreApplication::translate(
            "PromptTemplateLibrary", "Could not write %1").arg(path));
    }
    const QByteArray bytes =
        PromptTemplateIo::serialize(template_).toUtf8();
    const qint64 written = file.write(bytes);
    file.close();
    if (written != bytes.size()) {
        return core::OpResult::failure(QCoreApplication::translate(
            "PromptTemplateLibrary", "Could not write %1").arg(path));
    }
    refresh();
    return core::OpResult::success();
}

/**
 * @brief 删除一份用户模板
 *
 * 内置条目没有用户文件可删（返回成功并刷新——内置回到随包形态本来
 * 就是它）。
 */
core::OpResult PromptTemplateLibrary::remove(const QString &id)
{
    if (id.isEmpty()) {
        return core::OpResult::failure(QCoreApplication::translate(
            "PromptTemplateLibrary", "A template id is required."));
    }
    const QString path =
        userDirectory() + QLatin1Char('/') + id + QStringLiteral(".md");
    if (QFile::exists(path) && !QFile::remove(path)) {
        return core::OpResult::failure(QCoreApplication::translate(
            "PromptTemplateLibrary", "Could not remove %1").arg(path));
    }
    refresh();
    return core::OpResult::success();
}

/**
 * @brief 用户模板目录
 */
QString PromptTemplateLibrary::userDirectory() const
{
    return m_dataRoot + QStringLiteral("/prompt_templates");
}

/**
 * @brief 装载内置模板（随包 JSON）
 */
void PromptTemplateLibrary::loadBuiltin()
{
    const QJsonObject root = core::JsonStore::readFile(
        QLatin1String(kBuiltinResource));
    const QJsonArray templates =
        root.value(QStringLiteral("templates")).toArray();
    for (const QJsonValue &value : templates) {
        if (!value.isObject()) {
            continue;
        }
        PromptTemplate template_ = builtinFromJson(value.toObject());
        if (template_.id.isEmpty() || template_.body.isEmpty()) {
            qWarning() << "[tools] builtin template skipped (no id or body):"
                       << template_.title;
            continue;
        }
        if (template_.title.isEmpty()) {
            template_.title = template_.id;
        }
        m_templates.append(template_);
    }
}

/**
 * @brief 装载用户模板并按 id 覆盖内置
 *
 * 坏文件（读不了的、头部残缺的）跳过并记警告——用户手改的目录，
 * 一个坏文件不该拖垮整个模板库。非 .md 后缀忽略。
 */
void PromptTemplateLibrary::loadUser()
{
    QDir directory(userDirectory());
    const QFileInfoList entries =
        directory.entryInfoList({ QStringLiteral("*.md") },
                                QDir::Files, QDir::Name);
    for (const QFileInfo &entry : entries) {
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "[tools] user template unreadable:"
                       << entry.absoluteFilePath();
            continue;
        }
        const QString raw = QString::fromUtf8(file.readAll());
        file.close();
        const QString fallbackId =
            entry.completeBaseName();
        const PromptTemplate template_ = PromptTemplateIo::parse(
            raw, fallbackId, PromptTemplate::Scope::User);
        if (template_.body.trimmed().isEmpty()) {
            qWarning() << "[tools] user template is empty, skipped:"
                       << entry.absoluteFilePath();
            continue;
        }
        // 覆盖内置：同 id 顶掉（用户的改写浮到内置原位，列表顺序不乱）。
        bool replaced = false;
        for (int i = 0; i < m_templates.size(); ++i) {
            if (m_templates.at(i).id == template_.id) {
                m_templates[i] = template_;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            m_templates.append(template_);
        }
    }
}

} // namespace awb::tools
