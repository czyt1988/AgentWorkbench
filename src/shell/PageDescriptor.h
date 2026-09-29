#ifndef AWB_SHELL_PAGEDESCRIPTOR_H
#define AWB_SHELL_PAGEDESCRIPTOR_H

#include <QString>

namespace awb::shell {

/// shell 眼中「一个工作区页面」的描述。
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
};

} // namespace awb::shell

#endif // AWB_SHELL_PAGEDESCRIPTOR_H
