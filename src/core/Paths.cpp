#include "core/Paths.h"

#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

namespace awb::core {

// Paths 是数据目录的唯一事实来源：所有子目录（themes/、plugins/、log/、
// webprofiles/）都由 dataRoot() 派生，别处不许拼这些路径。
//
// dataRoot() 的取值优先级：setDataRootForTesting() 注入的测试根 >
// QStandardPaths 测试模式的重定向位置 > 用户 home 下的 ~/.AgentWorkbench。
//
// 非 ASCII 用户目录（C:\Users\陈宗衍\…）只能交给 QFile/QDir 处理，
// 不许把这些路径传给窄字符 API。

QString Paths::s_testRoot;

/**
 * @brief 记录测试注入的数据根
 *
 * @param dir 注入的目录；空串表示清除覆盖、恢复默认取值
 */
void Paths::setDataRootForTesting(const QString &dir)
{
    s_testRoot = dir;
}

/**
 * @brief 判断 dataRoot() 是否被测试注入覆盖
 *
 * @return 已注入非空测试根时返回 true
 */
bool Paths::isDataRootOverridden()
{
    return !s_testRoot.isEmpty();
}

/**
 * @brief 取应用数据根目录
 *
 * 优先级：测试注入的根 > QStandardPaths 测试模式的重定向位置 >
 * home 目录下的 ~/.AgentWorkbench。
 *
 * @return 数据根的绝对路径，不以分隔符结尾
 */
QString Paths::dataRoot()
{
    if (!s_testRoot.isEmpty()) {
        return s_testRoot;
    }

    // QStandardPaths 的测试模式只重定向 App* 位置，不动 HomeLocation
    // （见 QStandardPaths::setTestModeEnabled），直接用 Home 的话单元测试
    // 会读写开发者真实的配置目录。测试模式下必须换用被重定向的位置。
    if (QStandardPaths::isTestModeEnabled()) {
        return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }

    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
           + QStringLiteral("/.AgentWorkbench");
}

/**
 * @brief 取主题目录
 *
 * @return <dataRoot>/themes
 */
QString Paths::themesDir()
{
    return dataRoot() + QStringLiteral("/themes");
}

/**
 * @brief 取插件目录
 *
 * @return <dataRoot>/plugins
 */
QString Paths::pluginsDir()
{
    return dataRoot() + QStringLiteral("/plugins");
}

/**
 * @brief 取日志目录
 *
 * @return <dataRoot>/log
 */
QString Paths::logsDir()
{
    return dataRoot() + QStringLiteral("/log");
}

/**
 * @brief 取 Web profile 根目录（每个 agent 一个子目录）
 *
 * @return <dataRoot>/webprofiles
 */
QString Paths::webProfilesDir()
{
    return dataRoot() + QStringLiteral("/webprofiles");
}

/**
 * @brief 取 Markdown 预览写出的自包含 HTML 目录
 *
 * @return <dataRoot>/preview
 */
QString Paths::markdownPreviewDir()
{
    return dataRoot() + QStringLiteral("/preview");
}

/**
 * @brief 取草稿的图片资源目录
 *
 * 没有工作区可绑定时，草稿粘贴的图片落在这里；它同时是草稿预览的资源
 * 根，因此插入的相对路径能直接渲染。draftId 来自可手改的 tools.json，
 * 这里只保留安全字符，避免把路径拼出数据目录之外。
 *
 * @param draftId 草稿 id；空串或全被过滤时退回 "draft"
 * @return <dataRoot>/draft-assets/<安全化后的 id>
 */
QString Paths::draftAssetsDir(const QString &draftId)
{
    static const QRegularExpression unsafe(
        QStringLiteral("[^A-Za-z0-9._-]"));
    QString safe = draftId;
    safe.replace(unsafe, QStringLiteral("_"));
    if (safe.isEmpty() || safe == QStringLiteral(".")
        || safe == QStringLiteral("..")) {
        safe = QStringLiteral("draft");
    }
    return dataRoot() + QStringLiteral("/draft-assets/") + safe;
}

/**
 * @brief 取 skill 扫描缓存文件
 *
 * @return <dataRoot>/skills_cache.json
 */
QString Paths::skillCacheFile()
{
    return dataRoot() + QStringLiteral("/skills_cache.json");
}

/**
 * @brief 取运行时探测缓存文件
 *
 * @return <dataRoot>/environment_cache.json
 */
QString Paths::environmentCacheFile()
{
    return dataRoot() + QStringLiteral("/environment_cache.json");
}

/**
 * @brief 取下载目录
 *
 * @return 系统下载目录（~/Downloads），尚未提供用户覆盖入口
 */
QString Paths::downloadsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

} // namespace awb::core
