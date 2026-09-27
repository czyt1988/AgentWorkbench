import QtQuick
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// One row of a settings/list column: surface background, control radius,
// subtle border and a content RowLayout with the standard row margins.
// Inject icon / text column / trailing controls as children; height comes
// from `rowHeight` (48 by default) or is overridden per instance.
Rectangle {
    id: control

    default property alias contentData: rowLayout.data

    property real rowHeight: 48

    radius: theme.radiusControl
    color: theme.surfaceBg
    border.color: theme.borderSubtle
    border.width: 1
    height: rowHeight

    RowLayout {
        id: rowLayout
        anchors.fill: parent
        anchors.leftMargin: theme.spacingM
        anchors.rightMargin: theme.spacingS
        spacing: theme.spacingM
    }
}
