import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Page header: title + subtitle on the left, page actions on the right
// Every page renders its own header except the Web page.
RowLayout {
    id: control

    property string title: ""
    property string subtitle: ""
    default property alias actions: actionsSlot.data

    Layout.fillWidth: true
    Layout.margins: theme.spacingL
    spacing: theme.spacingM

    ColumnLayout {
        spacing: theme.spacingXs

        Label {
            visible: control.title.length > 0
            text: control.title
            color: theme.textPrimary
            font.pixelSize: theme.fontSizePageTitle
            font.bold: true
        }
        Label {
            visible: control.subtitle.length > 0
            text: control.subtitle
            color: theme.textMuted
            font.pixelSize: theme.fontSizeBody
        }
    }

    Item {
        Layout.fillWidth: true
    }

    RowLayout {
        id: actionsSlot
        spacing: theme.spacingS
    }
}
