import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench.App
import AgentWorkbench

// Modal dialog skeleton: centered, overlay background,
// Esc closes, focus starts on the first interactive child.
Dialog {
    id: control

    property alias titleText: titleLabel.text
    default property alias contentData: contentColumn.data

    // danger: red title + red border, for destructive confirmations and
    // error alerts.
    property bool danger: false

    anchors.centerIn: parent
    modal: true
    focus: true
    padding: theme.spacingL
    width: 420
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: theme.overlayBg
        border.color: control.danger ? theme.danger : theme.borderSubtle
        border.width: 1
        radius: theme.radiusOverlay
    }

    // Content column: titles, body, then right-aligned action buttons
    // (the caller appends them as children, like the 0.3.0 popups did).
    contentItem: ColumnLayout {
        id: contentColumn
        spacing: theme.spacingM

        Label {
            id: titleLabel
            visible: text.length > 0
            color: control.danger ? theme.danger : theme.textPrimary
            font.pixelSize: theme.fontSizeSubtitle
            font.bold: true
        }
    }
}
