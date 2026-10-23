#ifndef AWB_WORKBENCH_DRAFTFILES_H
#define AWB_WORKBENCH_DRAFTFILES_H

#include <QList>
#include <QString>
#include <QStringList>

namespace awb::workbench {

/// Markdown 里的一处图片目标：start/end 是目标 token（含尖括号）在原文中的
/// 区间，target 是百分号解码后的文件系统写法。
struct DraftImageEdit
{
    int start = 0;
    int end = 0;
    QString target;
};

/// 一次图片迁移请求。
///
/// mode 取值（磁盘与 QML 共用同一组字符串）：
///   "assets"   —— 默认：图片移到工作区（或文档目录）下的 assets 目录，
///                 引用改成相对路径；
///   "folder"   —— 图片移到用户指定的目录，引用按盘符条件用相对或绝对；
///   "absolute" —— 图片不移动，引用保留/改为绝对路径。
struct DraftImageMigrationRequest
{
    QString markdownPath;   ///< 保存到的 .md 绝对路径（相对引用以它为基准）
    QString workspace;      ///< 当前工作区；空串 = 没有工作区
    QString draftRoot;      ///< 草稿资源根（<dataRoot>/draft-assets/<id>）
    QString assetsSubdir;   ///< 默认图片目录名（settings 的 imageAssetsDir）
    QString mode = QStringLiteral("assets");
    QString folder;         ///< mode = "folder" 时的目标目录（绝对路径）
};

/// 图片迁移结果。moved/reused 是计数，warning 是面向用户的提示（跨盘符
/// 只能写绝对路径时给出），error 是失败原因。
struct DraftImageMigrationResult
{
    bool ok = true;
    QString text;       ///< 改写后的 Markdown
    QString error;
    QString warning;
    int moved = 0;
    int reused = 0;
    QStringList targets;  ///< 迁移后图片的最终路径
};

/// 草稿与磁盘 Markdown 之间的文件操作：默认保存路径、图片迁移、草稿资源
/// 目录的列举与清理。QML 不碰文件系统，这些动作统一由 workbench 层供出。
class DraftFiles
{
public:
    // 草稿标题 → 可用的 Windows 文件名（非法字符替换为下划线）
    static QString sanitizedBaseName(const QString &title);
    // 目录 + 标题 → 默认的 .md 保存路径；目录为空时返回空串
    static QString defaultSavePath(const QString &directory,
                                   const QString &title);
    // 默认图片目录：文档在工作区内时取工作区下的 assetsSubdir，否则取
    // 文档目录下的 assetsSubdir（迁移选项 1 的落点）
    static QString defaultAssetsFolder(const QString &markdownPath,
                                       const QString &workspace,
                                       const QString &assetsSubdir);

    // 抽出 Markdown 里的全部图片目标（不判断存在性）
    static QList<DraftImageEdit> imageDestinations(const QString &markdown);

    // 引用到的、位于 draftRoot 内的本地图片文件（去重；保存后据此决定
    // 要不要弹图片迁移界面）
    static QStringList referencedAssets(const QString &markdown,
                                        const QString &markdownPath,
                                        const QString &draftRoot);

    // 草稿资源根下的全部文件（绝对路径，排序稳定）
    static QStringList listAssets(const QString &draftRoot);
    // 清空草稿资源根下的全部内容（保留根目录本身）；无论是否被文档引用
    static bool clearAssets(const QString &draftRoot);

    // 按 request 迁移图片并改写引用；只处理位于 draftRoot 内的本地文件
    static DraftImageMigrationResult migrateImages(
        const QString &markdown, const DraftImageMigrationRequest &request);
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_DRAFTFILES_H
