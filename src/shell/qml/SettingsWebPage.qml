import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置页·Web 分区：内嵌/外部表面选择与 Chromium 启动旗标。
ScrollView {
    id: page

    contentWidth: availableWidth
    ScrollBar.vertical: AScrollBar {}

    ColumnLayout {
        width: page.availableWidth
        spacing: theme.spacingM

        PageHeader {
            title: qsTr("Web")
            subtitle: qsTr("How agent web UIs are displayed")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Surface")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
            }
            AComboBox {
                id: surfaceCombo
                textRole: "text"
                valueRole: "value"
                // Without WebEngine only the external surface exists
                model: web.engineAvailable
                       ? [{ text: qsTr("Embedded (in-app)"), value: "embedded" },
                          { text: qsTr("External (system browser)"), value: "external" }]
                       : [{ text: qsTr("External (system browser)"), value: "external" }]
                Component.onCompleted: currentIndex =
                    indexOfValue(shell.webSurface)
                onActivated: shell.setWebSurface(currentValue)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingM

            ATextField {
                id: flagsField
                Layout.fillWidth: true
                text: shell.webChromiumFlags
                placeholderText: qsTr("Chromium flags, e.g. --disable-gpu (applies after restart)")
                font.family: theme.monoFamily
                font.pixelSize: theme.fontSizeCaption
                onEditingFinished: shell.setWebChromiumFlags(text.trim())
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            text: qsTr("If embedded views fail to start (GPU driver issues), add --disable-gpu here. The in-app 'Open in browser' action always works as a fallback.")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeCaption
            wrapMode: Text.WordWrap
        }
    }
}
