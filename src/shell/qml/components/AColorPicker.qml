import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AgentWorkbench
import AgentWorkbench.App

// 通用颜色选择器（Office/WPS 式布局），颜色选择场景一律复用本组件：
//
//   主题色    10 列网格：第一行为主题色（ui.colorThemes 的前 10 个），
//             空一小格间隙后第 2~6 行是各列主题色的深浅阶（HSL 明度等
//             比缩放，色相/饱和度保持不变）；
//   标准色    固定 10 色（ui.standardColors）；
//   自定义    最近用过的自定义颜色（ui.recentColors，进程内最多 10 个）
//             +「More Colors...」——弹系统颜色对话框（ui.pickColor），
//             选定后经 ui.rememberColor 记进最近记忆。
//
// 用法：声明实例，openBelow(锚点 item) 弹出（自动下方对齐、放不下翻上
// 方、右缘越界左移）；点任何色块发 picked("#rrggbb") 并关闭；allowNone
// 时首行有「No color」，点中发 cleared()。数据与记忆都在 ui（UiServices）
// 里，组件不落盘。
Popup {
    id: picker

    // 当前颜色（#rrggbb；空串或非法串 = 无选中标记）。
    property string currentHex: ""
    // 允许「No color」选项（表单里空 = 跟随默认/自动分配的场景）。
    property bool allowNone: false

    signal picked(string hex)
    signal cleared()

    padding: theme.spacingM
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // 弹层尺寸由内容撑开（各区块均为固有尺寸的色块网格）。
    width: content.implicitWidth + leftPadding + rightPadding
    height: content.implicitHeight + topPadding + bottomPadding

    // --- 主题色的深浅阶 ---------------------------------------------------
    // 明度系数（乘在主题色 L 上，结果钳制在 0.07~0.93）：从浅到深排，
    // 主题色本体（第一行）上下各覆盖两三档。这里做的是纯颜色数学，与
    // 主题深浅变体无关，所以不走 theme.hover/pressed 派生色。
    readonly property var shadeFactors: [1.4, 1.18, 0.88, 0.68, 0.5]
    // 主题色（最多 10 列——列数由色板长度决定，超出截断）。
    readonly property var themeColors: ui.colorThemes.slice(0, 10)
    // 深浅阶二维色阵：[row][col] = "#rrggbb" 串。
    readonly property var shadeRows: {
        const rows = []
        for (let r = 0; r < picker.shadeFactors.length; ++r) {
            const row = []
            for (let c = 0; c < picker.themeColors.length; ++c)
                row.push(shadeOf(picker.themeColors[c],
                                 picker.shadeFactors[r]))
            rows.push(row)
        }
        return rows
    }

    // 打开并锚定到 anchor（通常是触发的色卡）：弹层左缘与锚点对齐，
    // 下方放不下且上方更宽裕时翻到锚点上方，右缘越出窗口时左移。
    // Window.window 是附加属性（Qt 5 的 Control 没有 window 属性），
    // 锚点尚不在窗口里时按 (0, 下方) 兜底。
    function openBelow(anchor) {
        if (anchor === null)
            return
        parent = anchor
        let x = 0
        let y = anchor.height + theme.spacingS
        const win = anchor.Window.window
        if (win && win.contentItem) {
            const pos = anchor.mapToItem(win.contentItem, 0, 0)
            const below = win.contentItem.height - (pos.y + anchor.height)
            if (below < height && pos.y > below)
                y = -height - theme.spacingS
            if (pos.x + width > win.contentItem.width)
                x = win.contentItem.width - width - pos.x
        }
        picker.x = x
        picker.y = y
        open()
    }

    // 当前色比对（currentHex 可能是用户手输入的大写形式，两侧都归一）。
    function isCurrent(hex) {
        return hex.toLowerCase() === currentHex.trim().toLowerCase()
    }

    // --- 颜色数学（HSL 明度缩放，色相/饱和度保持） -------------------------
    function hexToRgb(hex) {
        const h = hex.replace("#", "")
        return [parseInt(h.substring(0, 2), 16) / 255,
                parseInt(h.substring(2, 4), 16) / 255,
                parseInt(h.substring(4, 6), 16) / 255]
    }

    function rgbToHsl(r, g, b) {
        const max = Math.max(r, g, b)
        const min = Math.min(r, g, b)
        let h = 0
        let s = 0
        const l = (max + min) / 2
        if (max !== min) {
            const d = max - min
            s = l > 0.5 ? d / (2 - max - min) : d / (max + min)
            if (max === r)
                h = (g - b) / d + (g < b ? 6 : 0)
            else if (max === g)
                h = (b - r) / d + 2
            else
                h = (r - g) / d + 4
            h /= 6
        }
        return { h: h, s: s, l: l }
    }

    function hueToRgb(p, q, t) {
        if (t < 0)
            t += 1
        if (t > 1)
            t -= 1
        if (t < 1 / 6)
            return p + (q - p) * 6 * t
        if (t < 1 / 2)
            return q
        if (t < 2 / 3)
            return p + (q - p) * (2 / 3 - t) * 6
        return p
    }

    function hslToRgb(h, s, l) {
        if (s === 0) {
            return [l, l, l]
        }
        const q = l < 0.5 ? l * (1 + s) : l + s - l * s
        const p = 2 * l - q
        return [hueToRgb(p, q, h + 1 / 3), hueToRgb(p, q, h),
                hueToRgb(p, q, h - 1 / 3)]
    }

    // 深浅阶的取色：hex → HSL → 明度乘系数 → RGB → "#rrggbb"。
    // 经 QColor 串行化会带上 alpha（#aarrggbb），故手工拼两位十六进制。
    function shadeOf(hex, factor) {
        const rgb = hexToRgb(hex)
        const hsl = rgbToHsl(rgb[0], rgb[1], rgb[2])
        const l = Math.max(0.07, Math.min(0.93, hsl.l * factor))
        const out = hslToRgb(hsl.h, hsl.s, l)
        function two(v) {
            const s = Math.round(v * 255).toString(16)
            return s.length === 1 ? "0" + s : s
        }
        return "#" + two(out[0]) + two(out[1]) + two(out[2])
    }

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
        // 玻璃底：与菜单/下拉弹层同族的观感（AComboBox/AColorPicker 各自
        // 独立实现，不引入组件间私有耦合）。
        Rectangle {
            anchors.fill: parent
            radius: theme.radiusOverlay
            color: theme.alpha(theme.surfaceBg,
                               theme.variant === "dark" ? 0.94 : 0.97)
            border.color: theme.borderSubtle
            border.width: 1
        }
    }

    contentItem: ColumnLayout {
        id: content
        spacing: theme.spacingS

        // --- 无颜色（表单里空 = 跟随默认） ----------------------------------
        Item {
            visible: picker.allowNone
            Layout.fillWidth: true
            implicitHeight: 26

            Rectangle {
                anchors.fill: parent
                radius: theme.radiusControl
                color: noneMouse.containsMouse ? theme.surfaceHoverBg
                                               : "transparent"
            }
            Row {
                anchors.left: parent.left
                anchors.leftMargin: theme.spacingXs
                anchors.verticalCenter: parent.verticalCenter
                spacing: theme.spacingS

                AColorSwatch {
                    side: 18
                    tooltip: ""
                }
                Label {
                    text: qsTr("No color")
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSizeBody
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            // 整行可点（含上面的装饰色块）：后声明的 MouseArea 盖在子项上。
            MouseArea {
                id: noneMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    picker.cleared()
                    picker.close()
                }
            }
        }

        // --- 主题色 ----------------------------------------------------------
        Label {
            text: qsTr("Theme Colors")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeSmall
            font.bold: true
        }
        Column {
            spacing: theme.spacingXs
            Layout.alignment: Qt.AlignLeft

            // 第一行：主题色本体。
            Row {
                spacing: theme.spacingXs
                Repeater {
                    model: picker.themeColors
                    delegate: AColorSwatch {
                        required property string modelData
                        colorValue: modelData
                        checked: picker.isCurrent(modelData)
                        tooltip: modelData
                        onClicked: {
                            picker.picked(modelData)
                            picker.close()
                        }
                    }
                }
            }
            // 与主题色本体之间的「小间隙」。
            Item { width: 1; height: theme.spacingS }
            // 第 2~6 行：各列主题色的深浅阶。
            Repeater {
                model: picker.shadeRows
                delegate: Row {
                    id: shadeRow

                    required property var modelData

                    spacing: theme.spacingXs
                    Repeater {
                        model: shadeRow.modelData
                        delegate: AColorSwatch {
                            required property string modelData
                            colorValue: modelData
                            checked: picker.isCurrent(modelData)
                            tooltip: modelData
                            onClicked: {
                                picker.picked(modelData)
                                picker.close()
                            }
                        }
                    }
                }
            }
        }

        // --- 标准色 ----------------------------------------------------------
        Label {
            text: qsTr("Standard Colors")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeSmall
            font.bold: true
        }
        Row {
            spacing: theme.spacingXs
            Layout.alignment: Qt.AlignLeft

            Repeater {
                model: ui.standardColors
                delegate: AColorSwatch {
                    required property string modelData
                    colorValue: modelData
                    checked: picker.isCurrent(modelData)
                    tooltip: modelData
                    onClicked: {
                        picker.picked(modelData)
                        picker.close()
                    }
                }
            }
        }

        // --- 自定义颜色 ------------------------------------------------------
        Label {
            text: qsTr("Custom Colors")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeSmall
            font.bold: true
        }
        Row {
            visible: ui.recentColors.length > 0
            spacing: theme.spacingXs
            Layout.alignment: Qt.AlignLeft

            Repeater {
                model: ui.recentColors
                delegate: AColorSwatch {
                    required property string modelData
                    colorValue: modelData
                    checked: picker.isCurrent(modelData)
                    tooltip: modelData
                    onClicked: {
                        picker.picked(modelData)
                        picker.close()
                    }
                }
            }
        }
        // 还没有自定义色时的占位说明（否则区块只有标题会显得莫名其妙）。
        Label {
            visible: ui.recentColors.length === 0
            text: qsTr("Colors defined in the dialog show up here.")
            color: theme.textMuted
            font.pixelSize: theme.fontSizeCaption
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        AButton {
            Layout.fillWidth: true
            variant: "secondary"
            text: qsTr("More Colors...")
            onClicked: {
                const hex = ui.pickColor(picker.currentHex)
                if (hex.length === 0)
                    return
                ui.rememberColor(hex)
                picker.picked(hex)
                picker.close()
            }
        }
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
