#ifndef AWB_SHELL_PAGEDESCRIPTOR_H
#define AWB_SHELL_PAGEDESCRIPTOR_H

#include <QString>

namespace awb::shell {

// One workspace page as seen by the shell (01-architecture.md §4.7).
// Registered by BuiltinPages (or, later, by a plugin) — the shell itself
// knows nothing about agents, skills or web.
struct PageDescriptor
{
    QString id;
    QString title;      // English source string (translated at the edge)
    QString iconSource; // qrc:/icons/... URL
    QString source;     // QML URL, e.g. qrc:/qt/qml/AgentWorkbench/agents/…
    QString section = QStringLiteral("main"); // main | extensions | system
    int order = 0;
    QString badgeText;  // sidebar pill, empty = no badge
    bool enabled = true;
};

} // namespace awb::shell

#endif // AWB_SHELL_PAGEDESCRIPTOR_H
