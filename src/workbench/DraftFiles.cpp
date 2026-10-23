#include "workbench/DraftFiles.h"

#include "workbench/ImageImporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QUrl>

namespace awb::workbench {

namespace {

QString toSlashes(const QString &path)
{
    QString result = path;
    result.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return result;
}

/// 两个路径是否指向同一位置（Windows 上大小写不敏感）
bool samePath(const QString &left, const QString &right)
{
#ifdef Q_OS_WIN
    return QDir::cleanPath(left).compare(QDir::cleanPath(right),
                                         Qt::CaseInsensitive) == 0;
#else
    return QDir::cleanPath(left) == QDir::cleanPath(right);
#endif
}

/// 外部引用（http/data/锚点/协议相对），迁移不碰它们
bool isExternalTarget(const QString &target)
{
    if (target.startsWith(QLatin1Char('#'))
        || target.startsWith(QStringLiteral("//"))) {
        return true;
    }
    // 至少两个字符才算协议名：单个字母是 Windows 盘符。
    static const QRegularExpression scheme(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]+:"));
    return scheme.match(target).hasMatch();
}

/// 文件名冲突时复用相同内容的已有文件，或追加 -1、-2……（移动语义）
bool moveInto(const QString &source, const QString &destination,
              bool *reused)
{
    if (samePath(source, destination)) {
        return true;
    }
    if (QFileInfo::exists(destination)) {
        if (!ImageImporter::filesEqual(source, destination)) {
            return false;
        }
        *reused = true;
        return QFile::remove(source);
    }
    if (QFile::rename(source, destination)) {
        return true;
    }
    // 跨卷 rename 会失败：复制成功后删除源文件，仍然是「移动」语义。
    if (!QFile::copy(source, destination)) {
        return false;
    }
    return QFile::remove(source);
}

/// 把一处图片目标解析成 draftRoot 内的本地文件；不在范围内返回空串
QString resolveDraftAsset(const QString &rawTarget, const QString &markdownDir,
                          const QString &draftRoot)
{
    QString target = rawTarget.trimmed();
    if (target.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl url(target);
        if (!url.isLocalFile()) {
            return QString();
        }
        target = url.toLocalFile();
    }
    else if (isExternalTarget(target)) {
        return QString();
    }
    QString source = target;
    if (!QFileInfo(source).isAbsolute()) {
        source = QDir(markdownDir).filePath(source);
    }
    source = QDir::cleanPath(source);
    if (!QFileInfo(source).isFile()
        || !ImageImporter::isInsideDirectory(source, draftRoot)) {
        return QString();
    }
    return source;
}

/// 递归清空目录内容（保留目录本身）
bool clearContents(const QDir &dir)
{
    bool ok = true;
    const QFileInfoList files = dir.entryInfoList(
        QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks);
    for (const QFileInfo &file : files) {
        ok = QFile::remove(file.absoluteFilePath()) && ok;
    }
    const QFileInfoList folders = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System
            | QDir::NoSymLinks);
    for (const QFileInfo &folder : folders) {
        ok = clearContents(QDir(folder.absoluteFilePath())) && ok;
        ok = QDir().rmdir(folder.absoluteFilePath()) && ok;
    }
    return ok;
}

} // namespace

/**
 * @brief 草稿标题 → 可用的文件名
 *
 * Windows 的非法文件名字符替换成下划线；首尾空白与末尾的点去掉（资源
 * 管理器不允许以点结尾的名字）。过滤后为空时退回 "draft"。
 */
QString DraftFiles::sanitizedBaseName(const QString &title)
{
    QString name = title.trimmed();
    static const QRegularExpression invalid(
        QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    name.replace(invalid, QStringLiteral("_"));
    while (name.endsWith(QLatin1Char('.'))
           || name.endsWith(QLatin1Char(' '))) {
        name.chop(1);
    }
    if (name.isEmpty()) {
        name = QStringLiteral("draft");
    }
    return name;
}

/**
 * @brief 默认保存路径：目录 + 草稿标题 + .md
 *
 * 标题本身已带 Markdown 后缀时不再叠加。目录为空（没有工作区也没有绑定
 * 目录）时返回空串，调用方据此走「另存为」。
 */
QString DraftFiles::defaultSavePath(const QString &directory,
                                    const QString &title)
{
    if (directory.trimmed().isEmpty()) {
        return QString();
    }
    QString name = sanitizedBaseName(title);
    const QString suffix = QFileInfo(name).suffix().toLower();
    if (suffix != QStringLiteral("md")
        && suffix != QStringLiteral("markdown")) {
        name += QStringLiteral(".md");
    }
    return QDir(directory).filePath(name);
}

/**
 * @brief 默认图片目录
 *
 * 文档位于当前工作区内时取「工作区/assetsSubdir」（用户要求的默认落点
 * 就是工作区）；否则退到文档所在目录下的 assetsSubdir。
 */
QString DraftFiles::defaultAssetsFolder(const QString &markdownPath,
                                        const QString &workspace,
                                        const QString &assetsSubdir)
{
    QString subdir;
    if (!ImageImporter::normalizeAssetsDir(assetsSubdir, &subdir)) {
        subdir = QStringLiteral("assets");
    }
    const QString markdownDir = QFileInfo(markdownPath).absolutePath();
    const bool insideWorkspace =
        !workspace.isEmpty()
        && ImageImporter::isInsideDirectory(markdownDir, workspace);
    return QDir(insideWorkspace ? workspace : markdownDir)
        .absoluteFilePath(subdir);
}

/**
 * @brief 抽出 Markdown 里的全部图片目标
 *
 * 认 `![alt](path)` 与 `![alt](<path with spaces>)` 两种写法（后者是导入器
 * 给带空白/圆括号路径生成的形态），可带 "title"。只做定位与解码，不判断
 * 文件是否存在。
 */
QList<DraftImageEdit> DraftFiles::imageDestinations(const QString &markdown)
{
    static const QRegularExpression pattern(
        QStringLiteral("!\\[[^\\]]*\\]\\(\\s*(?:<([^>]*)>|([^\\s()]+))"
                       "(?:[ \\t]+(?:\"[^\"]*\"|'[^']*'|\\([^()]*\\)))?"
                       "\\s*\\)"));
    QList<DraftImageEdit> edits;
    auto it = pattern.globalMatch(markdown);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        DraftImageEdit edit;
        if (match.hasCaptured(1)) {
            edit.start = match.capturedStart(1) - 1;
            edit.end = match.capturedEnd(1) + 1;
            edit.target = match.captured(1);
        }
        else {
            edit.start = match.capturedStart(2);
            edit.end = match.capturedEnd(2);
            edit.target = match.captured(2);
        }
        edit.target = QUrl::fromPercentEncoding(edit.target.toUtf8());
        edits.append(edit);
    }
    return edits;
}

/**
 * @brief 引用到的、位于草稿资源根内的本地图片
 *
 * 保存后判断「有没有要迁移的图片」用。重复引用同一文件只报一次。
 */
QStringList DraftFiles::referencedAssets(const QString &markdown,
                                         const QString &markdownPath,
                                         const QString &draftRoot)
{
    QStringList result;
    const QString markdownDir = QFileInfo(markdownPath).absolutePath();
    const QString root =
        QDir::cleanPath(QFileInfo(draftRoot).absoluteFilePath());
    const QList<DraftImageEdit> edits = imageDestinations(markdown);
    for (const DraftImageEdit &edit : edits) {
        const QString source =
            resolveDraftAsset(edit.target, markdownDir, root);
        if (!source.isEmpty() && !result.contains(source)) {
            result.append(source);
        }
    }
    return result;
}

/**
 * @brief 草稿资源根下的全部文件
 *
 * 递归列举，绝对路径、排序稳定（QDir 的默认名称序），供关闭草稿前的
 * 「将删除这些资源」列表使用。
 */
QStringList DraftFiles::listAssets(const QString &draftRoot)
{
    QStringList files;
    const QDir root(draftRoot);
    if (!root.exists()) {
        return files;
    }
    const QFileInfoList entries = root.entryInfoList(
        QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
        QDir::Name);
    for (const QFileInfo &entry : entries) {
        files.append(entry.absoluteFilePath());
    }
    const QFileInfoList folders = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System
            | QDir::NoSymLinks,
        QDir::Name);
    for (const QFileInfo &folder : folders) {
        files.append(listAssets(folder.absoluteFilePath()));
    }
    return files;
}

/**
 * @brief 清空草稿资源根（保留根目录本身）
 *
 * 「无论图片是否被文档引用都清除」：这是用户确认后的资源回收，不做引用
 * 分析。
 */
bool DraftFiles::clearAssets(const QString &draftRoot)
{
    QDir root(draftRoot);
    if (!root.exists()) {
        return true;
    }
    return clearContents(root);
}

/**
 * @brief 按请求迁移草稿图片并改写 Markdown 引用
 *
 * 只处理位于 draftRoot 之内的本地文件（草稿粘贴产生的那批）。目标目录
 * 由模式决定：assets 落在工作区或文档目录下、folder 用用户选的目录、
 * absolute 不移动。引用路径优先相对 markdownPath；跨盘符时退回绝对路径
 * 并在 warning 里说明。
 */
DraftImageMigrationResult DraftFiles::migrateImages(
    const QString &markdown, const DraftImageMigrationRequest &request)
{
    DraftImageMigrationResult result;
    result.text = markdown;
    if (request.markdownPath.trimmed().isEmpty()) {
        result.ok = false;
        result.error = QCoreApplication::translate(
            "DraftFiles", "The Markdown file has no save location.");
        return result;
    }
    const QString markdownDir =
        QFileInfo(request.markdownPath).absolutePath();
    const QString draftRoot =
        QDir::cleanPath(QFileInfo(request.draftRoot).absoluteFilePath());
    const bool absolute = request.mode == QLatin1String("absolute");

    QString destinationDir;
    if (!absolute) {
        if (request.mode == QLatin1String("folder")) {
            if (request.folder.trimmed().isEmpty()) {
                result.ok = false;
                result.error = QCoreApplication::translate(
                    "DraftFiles", "No image folder was chosen.");
                return result;
            }
            destinationDir =
                QDir::cleanPath(QFileInfo(request.folder).absoluteFilePath());
        }
        else {
            destinationDir = defaultAssetsFolder(
                request.markdownPath, request.workspace,
                request.assetsSubdir);
        }
        if (!QDir().mkpath(destinationDir)) {
            result.ok = false;
            result.error = QCoreApplication::translate(
                "DraftFiles", "Could not create the image folder: %1")
                .arg(destinationDir);
            return result;
        }
    }

    const QList<DraftImageEdit> edits = imageDestinations(markdown);
    if (edits.isEmpty()) {
        return result;
    }

    struct Rewrite
    {
        int start = 0;
        int end = 0;
        QString token;
    };
    QList<Rewrite> rewrites;
    QHash<QString, QString> movedBySource;
    QStringList failures;

    for (const DraftImageEdit &edit : edits) {
        const QString source =
            resolveDraftAsset(edit.target, markdownDir, draftRoot);
        if (source.isEmpty()) {
            continue;
        }
        const QFileInfo sourceInfo(source);
        const QString sourceKey = sourceInfo.canonicalFilePath().isEmpty()
                ? source : sourceInfo.canonicalFilePath();

        QString finalPath = movedBySource.value(sourceKey);
        if (finalPath.isEmpty()) {
            finalPath = source;
            if (!absolute
                && !samePath(sourceInfo.absolutePath(), destinationDir)) {
                finalPath = ImageImporter::uniqueDestination(
                    destinationDir, sourceInfo.fileName(), source);
                bool reused = false;
                if (!moveInto(source, finalPath, &reused)) {
                    failures.append(QCoreApplication::translate(
                        "DraftFiles", "Could not move the image %1")
                        .arg(source));
                    continue;
                }
                if (reused) {
                    ++result.reused;
                }
                else {
                    ++result.moved;
                }
            }
            movedBySource.insert(sourceKey, finalPath);
        }

        QString reference;
        if (absolute) {
            reference = finalPath;
        }
        else {
            reference = QDir(markdownDir).relativeFilePath(finalPath);
            static const QRegularExpression drivePath(
                QStringLiteral("^[A-Za-z]:[\\\\/]"));
            if (reference.isEmpty() || QDir::isAbsolutePath(reference)
                || drivePath.match(reference).hasMatch()) {
                // 跨盘符（或不在同一目录树）只能写绝对路径：相对路径要么
                // 不存在，要么会带盘符被 Markdown 当成协议。
                reference = finalPath;
                if (result.warning.isEmpty()) {
                    result.warning = QCoreApplication::translate(
                        "DraftFiles",
                        "The image folder is on another drive, so the "
                        "references use absolute paths.");
                }
            }
        }
        reference = toSlashes(reference);
        const QString token = ImageImporter::markdownDestination(reference);
        if (token != markdown.mid(edit.start, edit.end - edit.start)) {
            rewrites.append({edit.start, edit.end, token});
        }
        result.targets.append(finalPath);
    }

    // 从后往前替换：位置始终对应原文。
    for (int index = rewrites.size() - 1; index >= 0; --index) {
        const Rewrite &rewrite = rewrites.at(index);
        result.text.replace(rewrite.start, rewrite.end - rewrite.start,
                            rewrite.token);
    }
    if (!failures.isEmpty()) {
        result.ok = false;
        result.error = failures.join(QLatin1Char('\n'));
    }
    return result;
}

} // namespace awb::workbench
