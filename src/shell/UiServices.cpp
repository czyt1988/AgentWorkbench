#include "shell/UiServices.h"

#include <QClipboard>
#include <QColor>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QUrl>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#include <commdlg.h>
#endif

namespace awb::shell {

// UiServices 把系统级 UI 操作（剪贴板、桌面打开、文件管理器、原生目录
// 与颜色选择框）收拢成一个 QML 可调用的门面，另承载通用颜色选择器
// （AColorPicker）的支撑数据与最近自定义颜色的进程内记忆。可失败的操
// 作都返回 OpResult，失败原因已经是面向用户的 tr() 源串，调用方直接
// 展示即可。

namespace {

// 最近自定义颜色的记忆上限：颜色选择器「最近使用的颜色」行最多 10 格
constexpr int kMaxRecentColors = 10;

#ifdef Q_OS_WIN
/**
 * @brief 取应用顶层窗口的原生句柄，供模态对话框当宿主
 *
 * @return 顶层窗口的 HWND；窗口列表为空（只在窗口尚未创建的阶段）时
 *         返回 nullptr，对话框按无宿主处理
 */
HWND ownerHwnd()
{
    const QWindowList windows = QGuiApplication::topLevelWindows();
    if (windows.isEmpty()) {
        return nullptr;
    }
    return reinterpret_cast<HWND>(windows.constFirst()->winId());
}
#endif

} // namespace

/**
 * @brief 构造 UI 服务
 *
 * @param parent QObject 父项
 */
UiServices::UiServices(QObject *parent)
    : QObject(parent)
{
}

/**
 * @brief 复制文本到系统剪贴板
 *
 * @param text 要复制的文本
 * @return 成功返回 ok 的 OpResult；文本为空或剪贴板不可用时返回失败，
 *         错误信息可直接展示给用户
 */
core::OpResult UiServices::copyText(const QString &text)
{
    if (text.isEmpty()) {
        return core::OpResult::failure(tr("Nothing to copy."));
    }
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return core::OpResult::failure(tr("The clipboard is not available."));
    }
    clipboard->setText(text);
    return core::OpResult::success();
}

/**
 * @brief 用系统处理器打开一个 URL
 *
 * @param url 目标地址；无效或为空时直接返回失败
 * @return 成功返回 ok 的 OpResult；没有应用接受该 URL 时返回失败，
 *         错误信息内嵌 URL 原文
 */
core::OpResult UiServices::openExternalUrl(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty()) {
        return core::OpResult::failure(tr("Invalid URL."));
    }
    if (!QDesktopServices::openUrl(url)) {
        return core::OpResult::failure(
            tr("No application accepted %1.").arg(url.toString()));
    }
    return core::OpResult::success();
}

/**
 * @brief 在文件管理器中定位到一个文件
 *
 * Windows 上经 explorer /select 打开父文件夹并高亮该文件；其它平台
 * 退化为打开所在文件夹。
 *
 * @param path 目标文件路径
 * @return 成功返回 ok 的 OpResult；路径不存在或无法启动文件管理器时
 *         返回失败
 * @sa openFolder
 */
core::OpResult UiServices::revealFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return core::OpResult::failure(
            tr("The path does not exist: %1").arg(path));
    }

#ifdef Q_OS_WIN
    // explorer /select,<path> 会在其所在文件夹里高亮该文件
    const QString native = QDir::toNativeSeparators(info.absoluteFilePath());
    qint64 pid = 0;
    if (!QProcess::startDetached(QStringLiteral("explorer"),
                                 {QStringLiteral("/select,") + native},
                                 QString(), &pid)) {
        return core::OpResult::failure(tr("Could not open the file manager."));
    }
    return core::OpResult::success();
#else
    return openFolder(info.absolutePath());
#endif
}

/**
 * @brief 在文件管理器中打开一个文件夹
 *
 * @param path 目标目录
 * @return 成功返回 ok 的 OpResult；路径不存在、不是目录或无法打开时
 *         返回失败
 * @sa revealFile
 */
core::OpResult UiServices::openFolder(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        return core::OpResult::failure(
            tr("Not a directory: %1").arg(path));
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()))) {
        return core::OpResult::failure(
            tr("Could not open the folder: %1").arg(path));
    }
    return core::OpResult::success();
}

/**
 * @brief 弹系统「选文件夹」对话框
 *
 * 走 Win32 IFileDialog（FOS_PICKFOLDERS），同一实现在 Qt 5/Qt 6 都
 * 可用：Qt 5 没有 Controls 的 FolderDialog，Qt.labs.platform 又强依赖
 * QApplication（本项目是 QGuiApplication），原生对话框是唯一能在
 * 两个版本下行为一致的路线。
 *
 * @param title 对话框标题；空串用系统默认
 * @return 所选目录的绝对路径；取消返回空串
 */
QString UiServices::pickFolder(const QString &title)
{
#ifdef Q_OS_WIN
    // IFileOpenDialog + FOS_PICKFOLDERS：Vista 之后的标准目录选择框，Qt 6
    // 的 FolderDialog 在 Windows 上内部走的也是它。COM STA 单元由 QPA 在
    // GUI 线程初始化，这里直接 CoCreateInstance 即可。对话框模态挂在顶层
    // 窗口上，运行自己的消息泵，期间主事件循环暂停——与 QML FolderDialog
    // 的模态行为一致。
    IFileDialog *dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_IFileDialog,
                                  reinterpret_cast<void **>(&dialog));
    if (FAILED(hr)) {
        qWarning() << "[ui] pickFolder: CoCreateInstance failed" << hr;
        return QString();
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (!title.isEmpty()) {
        dialog->SetTitle(reinterpret_cast<const wchar_t *>(title.utf16()));
    }

    HWND parent = ownerHwnd();

    QString result;
    hr = dialog->Show(parent);
    if (SUCCEEDED(hr)) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))
                && path) {
                result = QString::fromWCharArray(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
#else
    // 非 Windows 暂无实现（QFileDialog 需要 QtWidgets）。返回空串按
    // 「用户取消」处理，调用方已有的空路径分支会静默返回。
    Q_UNUSED(title);
    qWarning() << "[ui] pickFolder is only implemented on Windows";
    return QString();
#endif
}

/**
 * @brief 弹系统「选颜色」对话框
 *
 * Windows 上走 comdlg32 的 ChooseColor——QColorDialog 需要 QtWidgets，
 * 本项目是 QGuiApplication；与 pickFolder 同理，原生对话框是 Qt 5 /
 * Qt 6 行为一致的唯一路线。CC_FULLOPEN 直接展开「规定自定义颜色」区，
 * CC_RGBINIT 把初始色回显到色矩阵。对话框模态挂在顶层窗口上、运行自
 * 己的消息泵，期间主事件循环暂停——与 pickFolder 一致。
 *
 * @param initialColor 初始回显色（#rrggbb；空串或非法时从黑色开始）
 * @return 所选颜色的 #rrggbb 串（小写）；取消或对话框打不开时返回空串
 */
QString UiServices::pickColor(const QString &initialColor)
{
#ifdef Q_OS_WIN
    // 16 个自定义色槽由对话框自己维护、跨调用保留（静态存储），重开对
    // 话框时上一次调的色还在。
    static COLORREF customSlots[16] = {};

    const QColor initial(initialColor.trimmed());
    CHOOSECOLORW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = ownerHwnd();
    dialog.lpCustColors = customSlots;
    if (initial.isValid()) {
        dialog.rgbResult = RGB(initial.red(), initial.green(),
                               initial.blue());
    }
    dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
    // 取消与失败都按「未选」返回空串：调用方已有的空色分支会静默收场。
    if (!ChooseColorW(&dialog)) {
        return QString();
    }
    return QColor(GetRValue(dialog.rgbResult), GetGValue(dialog.rgbResult),
                  GetBValue(dialog.rgbResult)).name();
#else
    // 非 Windows 暂无实现（QColorDialog 需要 QtWidgets）。返回空串按
    // 「用户取消」处理。
    Q_UNUSED(initialColor);
    qWarning() << "[ui] pickColor is only implemented on Windows";
    return QString();
#endif
}

/**
 * @brief 把一个自定义颜色记进最近记忆
 *
 * 与 Office/WPS 的「最近使用的颜色」同规格：最新在队首；重复选色先删
 * 旧再插队首；最多保留 kMaxRecentColors 条。只在记忆真正变化时发
 * recentColorsChanged，订阅方（AColorPicker 的最近行）不无谓重排。记
 * 忆只活在当前进程，不落盘。
 *
 * @param color 颜色，#rrggbb（QColor 认得的其它写法也会被归一成它）；
 *              空串或非法颜色直接忽略
 */
void UiServices::rememberColor(const QString &color)
{
    const QColor parsed(color.trimmed());
    if (!parsed.isValid()) {
        return;
    }
    const QString name = parsed.name();
    if (!m_recentColors.isEmpty() && m_recentColors.first() == name) {
        return;
    }
    m_recentColors.removeAll(name);
    m_recentColors.prepend(name);
    while (m_recentColors.size() > kMaxRecentColors) {
        m_recentColors.removeLast();
    }
    Q_EMIT recentColorsChanged();
}

/**
 * @brief 最近用过的自定义颜色
 *
 * @return #rrggbb 串列表，最新在前；尚无记忆时为空表
 */
QStringList UiServices::recentColors() const
{
    return m_recentColors;
}

/**
 * @brief 颜色选择器的默认主题色
 *
 * @return 10 个常见色相的 #rrggbb 串；选择器按列展示，每列由该色派生
 *         5 档深浅
 */
QStringList UiServices::colorThemes() const
{
    static const QStringList themes = {
        QStringLiteral("#c0392b"),   // 红
        QStringLiteral("#e67e22"),   // 橙
        QStringLiteral("#f1c40f"),   // 琥珀黄
        QStringLiteral("#27ae60"),   // 绿
        QStringLiteral("#16a085"),   // 青绿
        QStringLiteral("#2980b9"),   // 蓝
        QStringLiteral("#3f51b5"),   // 靛蓝
        QStringLiteral("#8e44ad"),   // 紫
        QStringLiteral("#d81b60"),   // 品红
        QStringLiteral("#7f8c8d"),   // 石板灰
    };
    return themes;
}

/**
 * @brief 颜色选择器的固定标准色
 *
 * @return Office 惯例的 10 个标准色 #rrggbb 串
 */
QStringList UiServices::standardColors() const
{
    static const QStringList standards = {
        QStringLiteral("#c00000"),   // 深红
        QStringLiteral("#ff0000"),   // 红
        QStringLiteral("#ffc000"),   // 橙
        QStringLiteral("#ffff00"),   // 黄
        QStringLiteral("#92d050"),   // 浅绿
        QStringLiteral("#00b050"),   // 绿
        QStringLiteral("#00b0f0"),   // 浅蓝
        QStringLiteral("#0070c0"),   // 蓝
        QStringLiteral("#002060"),   // 深蓝
        QStringLiteral("#7030a0"),   // 紫
    };
    return standards;
}

} // namespace awb::shell
