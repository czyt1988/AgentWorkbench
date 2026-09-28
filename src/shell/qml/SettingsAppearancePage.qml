import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置页·外观分区：主题选择。分区页共用同一骨架（ScrollView +
// ColumnLayout + PageHeader），由 SettingsPage 的 StackLayout 装载。
ScrollView {
    id: page

    contentWidth: availableWidth
    ScrollBar.vertical: AScrollBar {}

    ColumnLayout {
        width: page.availableWidth
        spacing: theme.spacingM

        PageHeader {
            title: qsTr("Appearance")
            subtitle: qsTr("Colors and typography of the application")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Theme")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
            }

            ComboBox {
                id: themeCombo

                // 宽度自适应内容：内置主题只有 Dark/Light 两个短标签，
                // 定宽会让选择框跟标签脱节或截断文字。
                textRole: "display"
                valueRole: "id"
                model: theme.availableThemes

                // 不能用 indexOfValue(theme.themeId)：那个绑定只依赖
                // themeId，页面重建时 ComboBox 内部 delegateModel 未就绪、
                // 求值成 -1 后永不重算（Qt 5 / Qt 6 都会这样），选择框
                // 一直是空的。遍历 NOTIFY 属性 theme.availableThemes 的
                // 写法在列表就绪时绑定会重算，回显正确。
                currentIndex: themeIndexOf(theme.themeId)
                onActivated: theme.applyTheme(currentValue)

                // 短标签损失了主题全名，悬停补一条 tooltip。
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: currentThemeName()

                function themeIndexOf(id) {
                    const themes = theme.availableThemes
                    for (let i = 0; i < themes.length; ++i) {
                        if (themes[i].id === id)
                            return i
                    }
                    return -1
                }

                function currentThemeName() {
                    const themes = theme.availableThemes
                    return currentIndex >= 0 && currentIndex < themes.length
                            ? themes[currentIndex].name
                            : ""
                }
            }
        }
    }
}
