import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AgentWorkbench
import AgentWorkbench.App

// 设置页·外观分区：主题选择、跟随系统深浅色、全局字体。分区页共用
// 同一骨架（ScrollView + ColumnLayout + PageHeader），由 SettingsPage 的
// StackLayout 装载。
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

            AComboBox {
                id: themeCombo

                // 跟随系统时主题由系统深浅色决定，显式选择被搁置——
                // 置灰以免选了不生效。
                enabled: !theme.followSystem

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

        // 跟随系统深浅色：开启时按系统深/浅套对应基线主题，上方的显式
        // 选择被搁置。Qt 5 路线探测不了系统配色，开关置灰。
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Follow system color scheme")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
            }
            Switch {
                enabled: theme.canFollowSystem
                checked: theme.followSystem
                onToggled: theme.setFollowSystem(checked)
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            visible: theme.canFollowSystem
            text: theme.followSystem
                  ? qsTr("The theme follows the system light/dark setting; the selection above is ignored until you turn this off.")
                  : ""
            color: theme.textMuted
            font.pixelSize: theme.fontSizeCaption
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            spacing: theme.spacingM

            Label {
                Layout.fillWidth: true
                text: qsTr("Font")
                color: theme.textPrimary
                font.pixelSize: theme.fontSizeBody
            }

            AComboBox {
                id: fontCombo

                // 首项跟随主题/系统默认（值空串），之后是本机全部字体族。
                // 按生效字体回显（设置覆盖 → 主题声明 → 空串），index 用
                // 遍历法而不是 indexOfValue——同样的 delegateModel 时序
                // 问题，见上面的主题选择框。
                textRole: "display"
                valueRole: "value"
                model: [{ display: qsTr("Theme default"), value: "" }]
                        .concat(theme.fontFamilies.map(
                                    function(family) {
                                        return { display: family,
                                                 value: family }
                                    }))
                currentIndex: fontIndexOf(theme.family)
                onActivated: theme.setFontFamily(currentValue)

                // 选中项可能很长：收缩宽度按内容走，剩余让给说明标签。
                Layout.maximumWidth: 260

                function fontIndexOf(family) {
                    const list = fontCombo.model
                    for (let i = 0; i < list.length; ++i) {
                        if (list[i].value === family)
                            return i
                    }
                    return -1
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: theme.spacingL
            Layout.rightMargin: theme.spacingL
            text: qsTr("The font applies to the whole application; \"Theme default\" follows the theme or the system font.")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeCaption
            wrapMode: Text.WordWrap
        }
    }
}
