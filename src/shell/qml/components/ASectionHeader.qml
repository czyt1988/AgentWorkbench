import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Settings section header: subtitle + bottom separator.
ColumnLayout {
    id: control

    property string text: ""
    property alias extra: extraSlot.children

    spacing: theme.spacingXs
    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true
        spacing: theme.spacingS

        Label {
            text: control.text
            color: theme.textSecondary
            font.pixelSize: theme.fontSizeSubtitle
            font.bold: true
        }
        Item {
            id: extraSlot
            Layout.fillWidth: true
            // Children provided via the `extra` alias (buttons, combos).
        }
    }

    Rectangle {
        Layout.fillWidth: true
        height: 1
        color: theme.separator
    }
}
