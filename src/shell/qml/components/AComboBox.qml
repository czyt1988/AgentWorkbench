import QtQuick
import QtQuick.Controls
import QtQuick.Window
import AgentWorkbench
import AgentWorkbench.App

// 全应用通用的下拉选择框：背景、内容、指示箭头与弹出列表全部走主题令
// 牌。Default/Basic 样式的调色板是硬编码浅色系（用户实测为 #E0E0E0 一
// 类的浅灰底），深色主题下与主题化文字叠成「浅底浅字」不可读——Agent
// Tools 的工作区选择框实际发生过。页面里一律用 AComboBox，不要再写裸
// ComboBox；需要定制省略方向或行内控件（如工作区行上的移除按钮）就在
// 使用点覆写 contentItem / delegate，颜色仍由本组件的主题令牌承载。
//
// 弹层沿用菜单的玻璃观感（半透明表面 + 外扩软阴影），与 AMenu 同族；
// 两者的背景做法在此各自独立实现（自包含，避免组件间私有耦合）。
ComboBox {
    id: control

    // 高度 32，与 AButton/ATextField 对齐；弹层内条目高 30。
    implicitHeight: 32
    padding: 0
    // 文本与两侧边缘的内距（contentItem 的右内距另需给指示箭头让位）。
    leftPadding: 10
    rightPadding: 8

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 32
        radius: theme.radiusControl
        color: control.enabled && (control.hovered || control.popup.visible)
               ? theme.surfaceHoverBg : theme.surfaceAltBg
        border.color: control.activeFocus ? theme.focusRing
                                         : theme.borderSubtle
        border.width: control.activeFocus ? 2 : 1
        opacity: control.enabled ? 1 : 0.5
    }

    contentItem: Text {
        text: control.displayText
        font.pixelSize: theme.fontSizeBody
        color: control.enabled ? theme.textPrimary : theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        // 文本区右缘给指示箭头让位（箭头贴右缘 8px 内距处）。
        rightPadding: control.indicator.width + theme.spacingXs
    }

    indicator: Text {
        x: control.width - width - control.rightPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        text: "\u25BE"
        color: control.enabled ? theme.textMuted : theme.textDisabled
        font.pixelSize: theme.fontSizeCaption
    }

    // 主题化默认条目：字符串列表直接显示 modelData，数组模型按 textRole
    // 取键（与 Basic 样式同语义）；使用点的自定义 delegate 会整体覆写。
    delegate: ItemDelegate {
        id: choice

        required property var modelData
        required property int index

        width: ListView.view.width
        implicitHeight: 30
        highlighted: control.highlightedIndex === index

        text: {
            if (control.textRole.length > 0 && typeof modelData === "object"
                    && modelData !== null)
                return String(modelData[control.textRole])
            return String(modelData)
        }

        background: Rectangle {
            color: choice.hovered || choice.highlighted
                   ? theme.surfaceHoverBg : "transparent"
        }
        contentItem: Text {
            leftPadding: theme.spacingS
            rightPadding: theme.spacingS
            width: choice.width - theme.spacingS - theme.spacingS
            text: choice.text
            color: theme.textPrimary
            font.pixelSize: theme.fontSizeBody
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    popup: Popup {
        y: control.height
        width: control.width
        padding: 1
        // 弹层不超出窗口：条目多时（字体清单）在弹层内滚动。Qt 5 的
        // Control 没有 window 属性（Qt 6 才有），跨版本统一走
        // Window.window 附加属性；不在窗口里（理论上不可能）时退固定上限。
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                         control.Window.window
                             ? control.Window.window.height - 80 : 320)

        background: Item {
            implicitWidth: 200
            implicitHeight: 200

            // 外扩软阴影一层：background 不裁剪子项，向四周扩。
            Rectangle {
                x: -3
                y: -2
                width: parent.width + 6
                height: parent.height + 6
                radius: theme.radiusOverlay + 3
                color: theme.alpha(theme.overlayBg, 0.18)
            }
            // 玻璃底：半透明表面让下层内容低对比透射（同 AMenu 的折衷）。
            Rectangle {
                anchors.fill: parent
                radius: theme.radiusOverlay
                color: theme.alpha(theme.surfaceBg,
                                   theme.variant === "dark" ? 0.94 : 0.97)
                border.color: theme.borderSubtle
                border.width: 1
            }
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            // 关闭时不挂 delegateModel，避免弹层隐藏期间产生多余条目。
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: AScrollBar {}
        }

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: theme.durationFast
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: theme.durationFast
            }
        }
    }
}
