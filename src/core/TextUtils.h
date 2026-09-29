#ifndef AWB_CORE_TEXTUTILS_H
#define AWB_CORE_TEXTUTILS_H

#include <QString>
#include <QStringList>

namespace awb::core {

/// 各模块共用的小型文本工具。
class TextUtils
{
public:
    // 从命令输出里提取 x.y.z 版本号（可带预发布后缀，如 "v1.2.3-beta"、"1.2.3.4"）
    static QString extractVersion(const QString &output);

    // 把程序与参数渲染成一条可复制粘贴的命令行，含空白或引号的参数加双引号。
    // "[cmd]" 日志行用它呈现真正执行了什么。
    static QString formatCommandLine(const QString &program,
                                     const QStringList &args = QStringList());

    // 把捕获的命令输出截断到 limit 字符并在有删减时追加提示，防止刷屏命令灌满日志
    static QString clampOutput(const QString &text, int limit);
};

} // namespace awb::core

#endif // AWB_CORE_TEXTUTILS_H
