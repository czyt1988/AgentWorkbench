#include "workbench/ImageImporter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>
#include <QRegularExpression>
#include <QUrl>

namespace awb::workbench {

namespace {

/// 把路径统一成 Markdown 用的正斜杠形态
QString toSlashes(const QString &path)
{
    QString result = path;
    result.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return result;
}

/// 语义等价的规范化路径：存在时用 canonical（Windows 大小写一致），
/// 不存在时退回落盘绝对路径的 cleanPath 形态
QString canonicalPath(const QString &path)
{
    const QString resolved = QFileInfo(path).canonicalFilePath();
    if (!resolved.isEmpty()) {
        return resolved;
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

/// 两个文件内容是否相同（先比大小，再分块比字节）
bool filesEqual(const QString &left, const QString &right)
{
    QFile leftFile(left);
    QFile rightFile(right);
    if (!leftFile.open(QIODevice::ReadOnly)
        || !rightFile.open(QIODevice::ReadOnly)) {
        return false;
    }
    if (leftFile.size() != rightFile.size()) {
        return false;
    }
    while (!leftFile.atEnd()) {
        const QByteArray leftBytes = leftFile.read(64 * 1024);
        const QByteArray rightBytes = rightFile.read(64 * 1024);
        if (leftBytes != rightBytes) {
            return false;
        }
    }
    return true;
}

/// 在目标目录里给源文件找一个不覆盖已有文件的落点；同名同内容直接复用
QString uniqueDestination(const QString &dir, const QString &fileName,
                          const QString &source)
{
    const QFileInfo nameInfo(fileName);
    const QString base = nameInfo.completeBaseName();
    const QString suffix = nameInfo.suffix();
    QString candidate = QDir(dir).filePath(fileName);
    int index = 0;
    while (QFileInfo::exists(candidate)) {
        if (filesEqual(source, candidate)) {
            return candidate;
        }
        ++index;
        const QString numbered = suffix.isEmpty()
            ? QStringLiteral("%1-%2").arg(base).arg(index)
            : QStringLiteral("%1-%2.%3").arg(base).arg(index).arg(suffix);
        candidate = QDir(dir).filePath(numbered);
    }
    return candidate;
}

/// 以 baseDir 为基准的相对路径（正斜杠）
QString relativeFor(const QString &baseDir, const QString &path)
{
    return toSlashes(QDir(baseDir).relativeFilePath(path));
}

/// 保存剪贴板位图并按同一套路径规则生成 Markdown
bool saveBitmap(const QImage &image, const QString &baseDir,
                const QString &assetsRoot, const QString &assetsSubdir,
                QString *markdown, QString *error)
{
    const QString assetsDir = QDir(assetsRoot).absoluteFilePath(assetsSubdir);
    if (!QDir().mkpath(assetsDir)) {
        *error = QCoreApplication::translate(
                     "ImageImporter", "Could not create the image folder: %1")
                     .arg(assetsDir);
        return false;
    }

    const QString stamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd-HHmmss"));
    QString fileName = QStringLiteral("image-%1.png").arg(stamp);
    QString path = QDir(assetsDir).filePath(fileName);
    int index = 0;
    while (QFileInfo::exists(path)) {
        ++index;
        fileName = QStringLiteral("image-%1-%2.png").arg(stamp).arg(index);
        path = QDir(assetsDir).filePath(fileName);
    }
    if (!image.save(path, "PNG")) {
        *error = QCoreApplication::translate(
                     "ImageImporter",
                     "Could not save the pasted image to %1")
                     .arg(path);
        return false;
    }

    const QString relative = relativeFor(baseDir, path);
    *markdown = ImageImporter::markdownForImage(
        relative, QFileInfo(path).completeBaseName());
    return true;
}

/// 处理一张图片文件：原地引用或复制进图片目录，再生成 Markdown
bool appendImage(const QString &sourcePath, const QString &baseDir,
                 const QString &assetsRoot, const QString &assetsSubdir,
                 QString *markdown, QString *error)
{
    const QFileInfo source(sourcePath);
    if (!source.isFile()) {
        *error = QCoreApplication::translate(
                     "ImageImporter", "Image file not found: %1")
                     .arg(sourcePath);
        return false;
    }

    QString target = source.absoluteFilePath();
    const bool inPlace =
        ImageImporter::isInsideDirectory(target, baseDir)
        || ImageImporter::isInsideDirectory(target, assetsRoot);
    if (!inPlace) {
        const QString assetsDir =
            QDir(assetsRoot).absoluteFilePath(assetsSubdir);
        if (!QDir().mkpath(assetsDir)) {
            *error = QCoreApplication::translate(
                         "ImageImporter",
                         "Could not create the image folder: %1")
                         .arg(assetsDir);
            return false;
        }
        const QString destination = uniqueDestination(
            assetsDir, source.fileName(), source.absoluteFilePath());
        if (!QFileInfo::exists(destination)
            && !QFile::copy(source.absoluteFilePath(), destination)) {
            *error = QCoreApplication::translate(
                         "ImageImporter", "Could not copy the image to %1")
                         .arg(destination);
            return false;
        }
        target = destination;
    }

    const QString relative = relativeFor(baseDir, target);
    if (relative.isEmpty() || QDir::isAbsolutePath(relative)) {
        *error = QCoreApplication::translate(
            "ImageImporter", "Could not build a relative image path.");
        return false;
    }
    *markdown = ImageImporter::markdownForImage(relative,
                                                source.completeBaseName());
    return true;
}

/// 多张图片每行一张；单张保持内联
QString joinImages(const QStringList &items)
{
    if (items.isEmpty()) {
        return QString();
    }
    if (items.size() == 1) {
        return items.constFirst();
    }
    return items.join(QLatin1Char('\n'));
}

} // namespace

/**
 * @brief 判断路径后缀是否属于可插入的图片格式
 *
 * 只比后缀，不读文件内容；是否存在由调用方另行判断。清单与预览 MIME
 * 表保持同一组格式，避免插入后预览不认。
 *
 * @param path 本地文件路径
 * @return 后缀命中时为真（大小写不敏感）
 */
bool ImageImporter::isImageFile(const QString &path)
{
    static const QStringList suffixes = {
        QStringLiteral("png"), QStringLiteral("jpg"),
        QStringLiteral("jpeg"), QStringLiteral("gif"),
        QStringLiteral("webp"), QStringLiteral("avif"),
        QStringLiteral("ico"), QStringLiteral("svg"),
        QStringLiteral("bmp") };
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

/**
 * @brief 判断路径是否位于某个目录之内
 *
 * 用 canonical 路径比较，Windows 上大小写不同的同一路径仍算命中；不同
 * 盘符时 QDir 给出的相对路径是绝对路径，这里一并判掉。
 *
 * @param path 待判断的路径
 * @param root 目录根
 * @return 在目录边界之内时为真（等于根本身不算）
 */
bool ImageImporter::isInsideDirectory(const QString &path,
                                      const QString &root)
{
    if (path.isEmpty() || root.isEmpty()) {
        return false;
    }
    const QString rel =
        QDir(canonicalPath(root)).relativeFilePath(canonicalPath(path));
    return !rel.isEmpty() && rel != QStringLiteral(".")
           && !rel.startsWith(QStringLiteral("../"))
           && !rel.startsWith(QStringLiteral("..\\"))
           && !QDir::isAbsolutePath(rel);
}

/**
 * @brief 规范化图片目录设置
 *
 * 空值回默认 assets；反斜杠归一成正斜杠；拒绝绝对路径、盘符与任何
 * `..` 段（它们能把落点带出工作区）。多级相对路径原样保留。
 *
 * @param raw 设置页里的原始文本
 * @param out 规范化结果（合法的空值也会写成 assets）
 * @return 合法时为真；out 为空指针或路径非法时为假
 */
bool ImageImporter::normalizeAssetsDir(const QString &raw, QString *out)
{
    if (!out) {
        return false;
    }
    QString dir = raw.trimmed();
    dir.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (dir.startsWith(QStringLiteral("./"))) {
        dir.remove(0, 2);
    }
    if (dir.isEmpty() || dir == QStringLiteral(".")) {
        *out = QStringLiteral("assets");
        return true;
    }
    if (QDir::isAbsolutePath(dir) || dir.startsWith(QLatin1Char('/'))
        || dir.contains(QLatin1Char(':'))) {
        return false;
    }
    // 先按原样拒绝 .. 段：`docs/../assets` 虽能 clean 成 assets，但设置里
    // 写 .. 本身就该被挡下，避免用户以为自己能跳出工作区。
    const QStringList parts = dir.split(QLatin1Char('/'));
    if (parts.contains(QStringLiteral(".."))) {
        return false;
    }
    const QString cleaned = QDir::cleanPath(dir);
    if (cleaned == QStringLiteral("..")
        || cleaned.startsWith(QStringLiteral("../"))
        || cleaned.contains(QStringLiteral("/../"))
        || cleaned.endsWith(QStringLiteral("/.."))) {
        return false;
    }
    *out = cleaned;
    return true;
}

/**
 * @brief 生成 Markdown 图片语法
 *
 * 目标里含空白或圆括号时用尖括号包住：CommonMark 的裸目标不允许这些
 * 字符。文件名里的 %、#、?、<、> 先自行百分号编码，预览侧先解码再编码
 * 的往返因此能还原成真实文件名。
 *
 * @param relativePath 相对路径（反斜杠会归一）
 * @param alt alt 文本（文件名去后缀）
 * @return `![alt](path)` 形态的文本
 */
QString ImageImporter::markdownForImage(const QString &relativePath,
                                        const QString &alt)
{
    QString path = toSlashes(relativePath);
    path.replace(QStringLiteral("%"), QStringLiteral("%25"));
    path.replace(QStringLiteral("#"), QStringLiteral("%23"));
    path.replace(QStringLiteral("?"), QStringLiteral("%3F"));
    path.replace(QStringLiteral("<"), QStringLiteral("%3C"));
    path.replace(QStringLiteral(">"), QStringLiteral("%3E"));

    static const QRegularExpression needsAngles(
        QStringLiteral("[\\s()]"));
    if (path.contains(needsAngles)) {
        path = QStringLiteral("<") + path + QStringLiteral(">");
    }

    QString text = alt;
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    text.replace(QLatin1Char('['), QStringLiteral("\\["));
    text.replace(QLatin1Char(']'), QStringLiteral("\\]"));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return QStringLiteral("![%1](%2)").arg(text, path);
}

/**
 * @brief 导入一批本地图片文件
 *
 * 非图片路径直接跳过；只要这批里出现过图片，handled 就为真，调用方不
 * 应再把它当作普通文本粘贴或文件引用。单张失败不影响其余图片，失败
 * 原因汇总在 error 里。
 *
 * @param paths 本地文件路径（绝对路径；QML 侧不负责 URL 转换）
 * @param baseDir 插入路径的基准目录（文档目录或草稿资源根）
 * @param assetsRoot 复制落点所在的根（工作区根或 baseDir）
 * @param assetsSubdir 根之下的图片目录（可多级）
 * @return 导入结果；handled 为假表示这批里没有图片
 */
ImageImportResult ImageImporter::importFiles(const QStringList &paths,
                                             const QString &baseDir,
                                             const QString &assetsRoot,
                                             const QString &assetsSubdir)
{
    ImageImportResult result;
    if (paths.isEmpty()) {
        return result;
    }

    QString subdir;
    if (!normalizeAssetsDir(assetsSubdir, &subdir)) {
        subdir = QStringLiteral("assets");
    }
    if (baseDir.isEmpty() || assetsRoot.isEmpty()) {
        result.handled = true;
        result.ok = false;
        result.error = QCoreApplication::translate(
            "ImageImporter", "No document folder is available for images.");
        return result;
    }

    QStringList items;
    QStringList failures;
    bool sawImage = false;
    for (const QString &path : paths) {
        if (!isImageFile(path)) {
            continue;
        }
        sawImage = true;
        QString markdown;
        QString error;
        if (appendImage(path, baseDir, assetsRoot, subdir, &markdown,
                        &error)) {
            items.append(markdown);
        } else {
            failures.append(error);
        }
    }
    if (!sawImage) {
        return result;
    }

    result.handled = true;
    result.ok = failures.isEmpty() && !items.isEmpty();
    result.markdown = joinImages(items);
    result.error = failures.join(QLatin1Char('\n'));
    return result;
}

/**
 * @brief 导入剪贴板内容
 *
 * 优先图片文件地址（资源管理器复制文件就是这条路）；其次在剪贴板没有
 * 可粘贴文本、没有 HTML 且带位图时保存成 PNG。其余情况返回 handled
 * 为假，由调用方交回编辑器原生粘贴。
 *
 * @param mime 剪贴板数据；空指针直接返回未接管
 * @param baseDir 插入路径的基准目录
 * @param assetsRoot 复制落点所在的根
 * @param assetsSubdir 根之下的图片目录
 * @return 导入结果
 */
ImageImportResult ImageImporter::importClipboard(const QMimeData *mime,
                                                 const QString &baseDir,
                                                 const QString &assetsRoot,
                                                 const QString &assetsSubdir)
{
    ImageImportResult result;
    if (!mime) {
        return result;
    }

    if (mime->hasUrls()) {
        QStringList paths;
        const QList<QUrl> urls = mime->urls();
        for (const QUrl &url : urls) {
            if (url.isLocalFile() && isImageFile(url.toLocalFile())) {
                paths.append(url.toLocalFile());
            }
        }
        if (!paths.isEmpty()) {
            return importFiles(paths, baseDir, assetsRoot, assetsSubdir);
        }
    }

    // 有文本（含网页复制带的 HTML）时交回编辑器：从浏览器复制图文混排，
    // 用户要的是原文本/链接，不是一张位图。
    if (!mime->text().trimmed().isEmpty()
        || !mime->html().trimmed().isEmpty()) {
        return result;
    }
    if (!mime->hasImage()) {
        return result;
    }

    QString subdir;
    if (!normalizeAssetsDir(assetsSubdir, &subdir)) {
        subdir = QStringLiteral("assets");
    }
    if (baseDir.isEmpty() || assetsRoot.isEmpty()) {
        result.handled = true;
        result.ok = false;
        result.error = QCoreApplication::translate(
            "ImageImporter", "No document folder is available for images.");
        return result;
    }

    const QImage image = qvariant_cast<QImage>(mime->imageData());
    if (image.isNull()) {
        result.handled = true;
        result.ok = false;
        result.error = QCoreApplication::translate(
            "ImageImporter", "The clipboard image is empty.");
        return result;
    }

    QString markdown;
    QString error;
    if (!saveBitmap(image, baseDir, assetsRoot, subdir, &markdown, &error)) {
        result.handled = true;
        result.ok = false;
        result.error = error;
        return result;
    }
    result.handled = true;
    result.markdown = markdown;
    return result;
}

} // namespace awb::workbench
