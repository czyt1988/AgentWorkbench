#include "tools/FileIcons.h"

#include "core/IconResolver.h"
#include "core/JsonStore.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

namespace awb::tools {

/// 磁盘格式的键名。内置表随可执行文件打包，测试目标把它编进自己的资源
/// （见 tests/tools/CMakeLists.txt），两边读到的是同一份文件。
namespace {
constexpr auto kBuiltinConfig = ":/config/default_file_icons.json";
constexpr auto kDefaultsKey = "defaults";
constexpr auto kFileNamesKey = "fileNames";
constexpr auto kSuffixesKey = "suffixes";
constexpr auto kFolderNamesKey = "folderNames";
constexpr auto kFileKey = "file";
constexpr auto kFolderKey = "folder";

/// 把一段 { 名字: 图标 } 并进目标表：键小写化（查表时同样小写），
/// 值经 IconResolver 归一，坏值退回 fallback，空值直接丢掉。
void mergeTable(const QJsonObject &object, QHash<QString, QString> *target,
                const QString &fallback)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!it.value().isString()) {
            continue;
        }
        const QString resolved =
                core::IconResolver::resolve(it.value().toString(), fallback);
        if (resolved.isEmpty()) {
            continue;
        }
        target->insert(it.key().toLower(), resolved);
    }
}

QString defaultIcon(const QJsonObject &defaults, const char *key)
{
    return defaults.value(QLatin1String(key)).toString();
}
} // namespace

/// 查表顺序：完整文件名 → 后缀 → 默认图标。文件名优先是为了 CMakeLists.txt、
/// Dockerfile、README 这类「后缀说明不了类型」的文件；后缀表按小写比对，
/// 因为 Windows/macOS 的大小写与磁盘上写的不一定一致。
FileIcons::FileIcons()
{
    merge(core::JsonStore::readFile(QLatin1String(kBuiltinConfig)));
    if (m_suffixes.isEmpty() && m_fileNames.isEmpty()) {
        qWarning() << "[tools] no file icon mappings loaded from"
                   << kBuiltinConfig;
    }
}

void FileIcons::loadUserFile(const QString &path)
{
    if (path.isEmpty() || !QFile::exists(path)) {
        return;
    }
    // 读不出来时 JsonStore 自己记一条警告并给出空对象，这里保持已有映射即可。
    merge(core::JsonStore::readFile(path));
}

QString FileIcons::forFile(const QString &fileName) const
{
    if (fileName.isEmpty()) {
        return m_fileFallback;
    }

    const auto byName = m_fileNames.constFind(fileName.toLower());
    if (byName != m_fileNames.constEnd()) {
        return byName.value();
    }

    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (!suffix.isEmpty()) {
        const auto bySuffix = m_suffixes.constFind(suffix);
        if (bySuffix != m_suffixes.constEnd()) {
            return bySuffix.value();
        }
    }
    return m_fileFallback;
}

QString FileIcons::forFolder(const QString &folderName) const
{
    if (folderName.isEmpty()) {
        return m_folderFallback;
    }
    const auto byName = m_folderNames.constFind(folderName.toLower());
    if (byName != m_folderNames.constEnd()) {
        return byName.value();
    }
    return m_folderFallback;
}

void FileIcons::merge(const QJsonObject &root)
{
    // defaults 先并：后面的表要用它当坏值的兜底。
    const QJsonObject defaults = root.value(QLatin1String(kDefaultsKey)).toObject();
    const QString fileDefault = defaultIcon(defaults, kFileKey);
    const QString folderDefault = defaultIcon(defaults, kFolderKey);
    if (!fileDefault.isEmpty()) {
        m_fileFallback = fileDefault;
    }
    if (!folderDefault.isEmpty()) {
        m_folderFallback = folderDefault;
    }

    mergeTable(root.value(QLatin1String(kFileNamesKey)).toObject(), &m_fileNames,
               m_fileFallback);
    mergeTable(root.value(QLatin1String(kSuffixesKey)).toObject(), &m_suffixes,
               m_fileFallback);
    mergeTable(root.value(QLatin1String(kFolderNamesKey)).toObject(),
               &m_folderNames, m_folderFallback);
}

} // namespace awb::tools
