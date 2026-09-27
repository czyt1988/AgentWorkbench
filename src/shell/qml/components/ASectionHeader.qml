import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Settings section header: subtitle + bottom separator, with optional
// trailing `extra` actions hugging the right edge (mirrors PageHeader).
ColumnLayout {
    id: control

    property string text: ""
    property alias extra: extraSlot.data

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

        // Spring pushing the extras to the right edge.
        Item {
            Layout.fillWidth: true
        }

        // Children provided via the `extra` alias (buttons, combos). It has
        // to be a Layout so injected children are positioned and vertically
        // centered — a plain Item left them stacked at its (0,0), overflowing
        // the zero-height row over the separator below.
        RowLayout {
            id: extraSlot
            spacing: theme.spacingS
        }
    }

    Rectangle {
        Layout.fillWidth: true
        height: 1
        color: theme.separator
    }
}
