#include "tools/FileIcons.h"

#include "core/IconResolver.h"
#include "core/JsonStore.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

namespace awb::tools {

namespace {

/**
 * @brief 内置映射表的 qrc 路径
 *
 * 随可执行文件打包；测试目标把同一份文件编进自己的资源
 * （见 tests/tools/CMakeLists.txt），两边读到的是同一份内容。
 */
constexpr auto kBuiltinConfig = ":/config/default_file_icons.json";

/**
 * @brief 磁盘 JSON 的段名与键名
 *
 * 键名是磁盘格式的一部分，改键名等于丢用户数据。defaults 段放兜底
 * 图标（file / folder 两个键），fileNames / suffixes / folderNames
 * 是三张「名字 → 图标」映射表的段名。
 */
constexpr auto kDefaultsKey = "defaults";
constexpr auto kFileNamesKey = "fileNames";
constexpr auto kSuffixesKey = "suffixes";
constexpr auto kFolderNamesKey = "folderNames";
constexpr auto kFileKey = "file";
constexpr auto kFolderKey = "folder";

/**
 * @brief 把一段 { 名字: 图标 } 对象并进目标表
 *
 * 键小写化（查表时同样按小写比对）；值经 IconResolver 归一，坏值退回
 * fallback，解析为空的值直接丢掉，不留空串污染表。
 *
 * @param object 磁盘 JSON 里的一张 { 名字: 图标 } 表
 * @param target 目标查找表，就地修改
 * @param fallback 值解析失败时使用的兜底图标
 */
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

/**
 * @brief 从 defaults 段取一个兜底图标
 *
 * @param defaults 磁盘 JSON 的 defaults 段
 * @param key 键名（kFileKey / kFolderKey）
 * @return 对应的图标 URL；键缺失或不是字符串时为空串，是否沿用旧值由调用方决定
 */
QString defaultIcon(const QJsonObject &defaults, const char *key)
{
    return defaults.value(QLatin1String(key)).toString();
}
} // namespace

/**
 * @brief 读内置映射表并建立查找表
 *
 * 查表顺序：完整文件名 → 后缀 → 默认图标。文件名优先是为了
 * CMakeLists.txt、Dockerfile、README 这类「后缀说明不了类型」的文件；
 * 后缀表按小写比对，因为 Windows/macOS 磁盘上的实际大小写与用户看到
 * 的不一定一致。内置表一条映射都没读到时记警告（资源打包出了问题）。
 */
FileIcons::FileIcons()
{
    merge(core::JsonStore::readFile(QLatin1String(kBuiltinConfig)));
    if (m_suffixes.isEmpty() && m_fileNames.isEmpty()) {
        qWarning() << "[tools] no file icon mappings loaded from"
                   << kBuiltinConfig;
    }
}

/**
 * @brief 叠加一份用户图标配置
 *
 * 文件不存在时静默返回（用户没配置过是常态）；同名键覆盖内置表。
 *
 * @param path 用户配置文件路径（约定是 <dataRoot>/file_icons.json）
 */
void FileIcons::loadUserFile(const QString &path)
{
    if (path.isEmpty() || !QFile::exists(path)) {
        return;
    }
    // 读不出来时 JsonStore 自己记一条警告并给出空对象，这里保持已有映射即可。
    merge(core::JsonStore::readFile(path));
}

/**
 * @brief 查文件的图标 URL
 *
 * @param fileName 文件名（含后缀；只看名字，不含路径）
 * @return 图标 URL；无匹配时返回文件的兜底图标（内置 defaults.file）
 */
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

/**
 * @brief 查目录的图标 URL
 *
 * 目录没有「后缀」可查，只有整名匹配一级。
 *
 * @param folderName 目录名
 * @return 图标 URL；无匹配时返回目录的兜底图标（内置 defaults.folder）
 */
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

/**
 * @brief 把一份磁盘 JSON 并进成员表
 *
 * defaults 段先落位：后面三张表要用它当坏值的兜底。三张表逐段并进，
 * 同名键后者覆盖前者——构造先并内置表、loadUserFile() 再并用户配置，
 * 因此用户配置赢。
 *
 * @param root 磁盘 JSON 的根对象
 */
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
