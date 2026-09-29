#include "core/IconResolver.h"

#include "core/EnvExpander.h"

#include <QFileInfo>
#include <QUrl>

namespace awb::core {

/**
 * @brief 把配置里的图标字符串解析成可显示的图片 URL
 *
 * @param raw 配置中的原始图标串，可为空
 * @param fallback raw 为空或解析不出可用图片时的回退 URL
 * @return 可直接交给 QML Image.source 的 URL
 */
QString IconResolver::resolve(const QString &raw, const QString &fallback)
{
    if (raw.isEmpty()) {
        return fallback;
    }

    // 内置资源、远程 URL 与 file URL 无需加工，原样使用。
    if (raw.startsWith(QStringLiteral("qrc:/"))
        || raw.startsWith(QStringLiteral("http://"))
        || raw.startsWith(QStringLiteral("https://"))
        || raw.startsWith(QStringLiteral("file://"))) {
        return raw;
    }

    // 其余一律当本地文件路径：先展开环境变量与 ~，
    // 用户可以写 "%USERPROFILE%/icons/my-agent.svg" 这类形式。
    const QString expanded = EnvExpander::expand(raw);
    const QFileInfo fi(expanded);
    if (fi.exists()) {
        return QUrl::fromLocalFile(fi.absoluteFilePath()).toString();
    }

    // 文件不存在时回退，而不是显示空白。
    return fallback;
}

} // namespace awb::core
