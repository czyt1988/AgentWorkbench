#ifndef AWB_SHELL_PAGEDESCRIPTOR_H
#define AWB_SHELL_PAGEDESCRIPTOR_H

#include <QString>

namespace awb::shell {

/// 侧栏一行的种类。
enum class NavKind
{
    Page,   ///< 可选中的页面行
    Header  ///< 静态分组标签：不可点击、不可高亮、不作为目的地
};

/// shell 眼中「侧栏的一行」的描述：一个可打开的工作区页面，或一个
/// 静态分组标签（见 kind）。
///
/// 由 BuiltinPages 注册（将来也可由插件注册）——shell 本身不认识
/// agent、skill 或 web。字段是 NavigationModel 各 role 的直接来源，
/// page() 快照的键名与字段名一一对应。
struct PageDescriptor
{
    QString id;      ///< 注册键（NavigationModel 按它查重与检索）
    QString title;   ///< 英文源串（在展示边缘经 qsTr() 翻译）
    QString iconSource; ///< 图标的 qrc URL（qrc:/icons/...）
    QString source;  ///< 页面 QML 的 URL，如 qrc:/qt/qml/AgentWorkbench/agentcatalog/…
    QString section = QStringLiteral("main"); ///< 侧栏分节：main | extensions | system
    int order = 0;   ///< 节内排序键，同分保持注册顺序（稳定排序）
    QString badgeText; ///< 侧栏徽标文本；空串 = 无徽标
    bool enabled = true; ///< false 时侧栏不显示该页
    /// 为 true 时该页由 Workspace 常驻托管：实例化一次后切换只是隐藏，
    /// 不再销毁。用于页面状态无法搬进 C++ 的页（Web 页的 WebEngineView
    /// 销毁即整页重载）。常驻页的快捷键必须自行在非当前页时禁用。
    bool keepAlive = false;
    /// 侧栏行的种类。NavKind::Header 是静态分组标签——没有 source、
    /// 不进快捷键序列、不能成为当前页，只负责给它下面那些行起个名字。
    NavKind kind = NavKind::Page;
    /// 侧栏缩进层级：0 = 顶层，1 = 挂在上一行之下的条目（分组标签下的
    /// 页面、将来二级菜单的子项都用它，侧栏按它缩进）。谁紧跟着谁仍然只
    /// 由 order 决定——分组标签要与它的条目 order 相邻，否则条目会跑到
    /// 别的分组底下去。
    int level = 0;
};

} // namespace awb::shell

#endif // AWB_SHELL_PAGEDESCRIPTOR_H
