#ifndef AWB_SHELL_PAGEDESCRIPTOR_H
#define AWB_SHELL_PAGEDESCRIPTOR_H

#include <QString>

namespace awb::shell {

// One workspace page as seen by the shell.
// Registered by BuiltinPages (or, later, by a plugin) — the shell itself
// knows nothing about agents, skills or web.
struct PageDescriptor
{
    QString id;
    QString title;      // English source string (translated at the edge)
    QString iconSource; // qrc:/icons/... URL
    QString source;     // QML URL, e.g. qrc:/qt/qml/AgentWorkbench/agentcatalog/…
    QString section = QStringLiteral("main"); // main | extensions | system
    int order = 0;
    QString badgeText;  // sidebar pill, empty = no badge
    bool enabled = true;
    /// 为 true 时该页由 Workspace 常驻托管：实例化一次后切换只是隐藏，
    /// 不再销毁。用于页面状态无法搬进 C++ 的页（Web 页的 WebEngineView
    /// 销毁即整页重载）。常驻页的快捷键必须自行在非当前页时禁用。
    bool keepAlive = false;
};

} // namespace awb::shell

#endif // AWB_SHELL_PAGEDESCRIPTOR_H
