#ifndef AWB_TOOLS_FILEICONS_H
#define AWB_TOOLS_FILEICONS_H

#include <QHash>
#include <QString>

class QJsonObject;

namespace awb::tools {

/// 文件树的图标映射表：文件名 / 后缀 / 目录名 → 图标 URL。
///
/// 映射本身是数据而不是代码（内置表随包，见 config/default_file_icons.json），
/// loadUserFile() 叠加用户配置，同名键用户赢。查表大小写不敏感，顺序是
/// 完整文件名 → 后缀 → 默认图标。值经 core::IconResolver 归一，坏值退回
/// 默认图标而不是留空。
class FileIcons
{
public:
    FileIcons();

    /// 叠加一份用户配置（约定是 <dataRoot>/file_icons.json）。
    /// 文件不存在时静默返回；读不出来时记一条警告并保留已有映射。
    void loadUserFile(const QString &path);

    /// 文件的图标 URL；无匹配时返回默认文件图标。
    QString forFile(const QString &fileName) const;
    /// 目录的图标 URL；无匹配时返回默认目录图标。
    QString forFolder(const QString &folderName) const;

private:
    void merge(const QJsonObject &root);

    /// 键一律小写；见 FileIcons.cpp 的查表顺序说明。
    QHash<QString, QString> m_fileNames;
    QHash<QString, QString> m_suffixes;
    QHash<QString, QString> m_folderNames;
    QString m_fileFallback;
    QString m_folderFallback;
};

} // namespace awb::tools

#endif // AWB_TOOLS_FILEICONS_H
