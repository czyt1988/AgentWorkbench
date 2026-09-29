#ifndef AWB_THEME_THEMEREGISTRY_H
#define AWB_THEME_THEMEREGISTRY_H

#include "theme/ThemeFile.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>

namespace awb::theme {

/// 可用主题的注册表：内置主题（:/themes/*.json）+ 用户主题
/// （<dataRoot>/themes/*.json），同 id 的用户文件覆盖内置版本。
///
/// 监视用户目录与其中的主题文件，保存主题文件即刻热重载。
class ThemeRegistry : public QObject
{
    Q_OBJECT

public:
    explicit ThemeRegistry(QObject *parent = nullptr);

    // 全部主题，顺序稳定：内置在前，用户主题按 id 排序在后
    QList<ThemeFile> themes() const;

    // 按 id 取生效主题（用户覆盖已应用）；未知 id 返回无效 ThemeFile
    ThemeFile theme(const QString &id) const;

    // 某 variant 的内置基线主题（"dark" -> mocha-dark，
    // "light" -> latte-light）；未知 variant 返回无效 ThemeFile
    ThemeFile baseline(const QString &variant) const;

    // 重扫内置与用户目录（并重挂文件监视）
    void refresh();

Q_SIGNALS:
    /**
     * @brief 主题文件在磁盘上变动（修改、新增、删除）后发射
     *
     * refresh() 重扫完成后发出；Theme 据此热重载当前主题。
     */
    void changed();

private:
    // 重扫全部主题来源，重建 m_themes / m_sources（不动监视器）
    void scan();

    // 重挂文件监视（先全部摘掉再加回，覆盖新增与消失的文件）
    void armWatchers();

    QHash<QString, ThemeFile> m_builtins;  ///< 内置主题（id -> 文件），用户主题的完整性基线
    QHash<QString, ThemeFile> m_themes;   ///< 生效主题：内置被同 id 用户文件覆盖或扩展
    QHash<QString, QString> m_sources;    ///< 每个生效主题的来源文件路径，挂监视用

    QFileSystemWatcher m_watcher;         ///< 用户主题目录与文件的监视器
};

} // namespace awb::theme

#endif // AWB_THEME_THEMEREGISTRY_H
