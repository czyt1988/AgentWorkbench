#ifndef AWB_WORKBENCH_IMAGEIMPORTER_H
#define AWB_WORKBENCH_IMAGEIMPORTER_H

#include <QString>
#include <QStringList>

class QMimeData;

namespace awb::workbench {

/// 拖入/粘贴图片的结果：handled 表示这次输入是否属于图片（false = 交回
/// 编辑器原生粘贴或文件引用），ok 表示落盘与路径生成是否成功。
struct ImageImportResult
{
    bool handled = false;
    bool ok = true;
    QString markdown;
    QString error;
};

/// 把图片文件或剪贴板位图变成 Markdown 图片语法。
///
/// 路径规则：图片在 baseDir 内原地引用；在 assetsRoot 内用 ../ 原地引用；
/// 两者之外复制进 assetsRoot/assetsSubdir 后再引用。默认所有插入路径都
/// 相对 baseDir；absolutePaths 为真时改写为图片的绝对路径——草稿没有磁盘
/// 文档，相对路径没有可解释的基准，用户看到 `assets/x.png` 也不知道它在
/// 数据目录的哪个角落。
class ImageImporter
{
public:
    // path 的后缀是否属于可插入的图片格式（大小写不敏感）
    static bool isImageFile(const QString &path);

    // 路径是否位于 root 目录之内（同盘、按目录边界判断）
    static bool isInsideDirectory(const QString &path, const QString &root);

    // 规范化图片目录设置：空值回默认 assets；拒绝绝对路径与 .. 段
    static bool normalizeAssetsDir(const QString &raw, QString *out);

    // 目标路径里的字符归一 + 必要时的尖括号（Markdown 图片目标语法）
    static QString markdownDestination(const QString &path);

    // 两个文件内容是否相同（迁移时的「同名就复用」判据）
    static bool filesEqual(const QString &left, const QString &right);

    // 在目标目录里给源文件找一个不覆盖已有文件的落点；同名同内容直接复用
    static QString uniqueDestination(const QString &dir,
                                     const QString &fileName,
                                     const QString &source);

    // 相对路径 + alt → ![alt](path)；必要时给目标加尖括号
    static QString markdownForImage(const QString &relativePath,
                                    const QString &alt);

    // 导入一批本地图片文件；absolutePaths 为真时插入绝对路径（草稿）
    static ImageImportResult importFiles(const QStringList &paths,
                                         const QString &baseDir,
                                         const QString &assetsRoot,
                                         const QString &assetsSubdir,
                                         bool absolutePaths = false);

    // 导入剪贴板内容：图片文件地址优先，其次无文本的纯位图
    static ImageImportResult importClipboard(const QMimeData *mime,
                                             const QString &baseDir,
                                             const QString &assetsRoot,
                                             const QString &assetsSubdir,
                                             bool absolutePaths = false);
};

} // namespace awb::workbench

#endif // AWB_WORKBENCH_IMAGEIMPORTER_H
