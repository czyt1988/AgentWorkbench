import QtQuick
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Toast host: bottom-right corner of the window (specs/02 §12).
AToastStack {
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    anchors.margins: theme.spacingL
}
