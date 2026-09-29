import QtQuick
import QtQuick.Controls
import AgentWorkbench
import AgentWorkbench.App

// 主题化上下文菜单：本仓库唯一的菜单形态——全部页面一律用 AMenu +
// AMenuItem（+ AMenuSeparator 分组），不要在各页面自写 Menu（Default
// 样式的 palette 是硬编码的浅色系，深色主题下高亮块与文字不可读）。
//
// 玻璃质感：半透明表面让下层内容隐约透出（Qt Quick 没有 backdrop
// blur，真模糊跨 Qt 大版本不可移植，半透明 + 顶部反光纱是折衷近似）；
// 外扩两层软阴影 + 内缘 1px 高光营造悬浮厚度感。不做指针光影跟随
// （那是 ASpotlight 的聚光职责，菜单不需要）。
//
// 顶部工具栏（如 markdown 格式化行）：直接声明普通 Item 作为第一个
// 子项即可——不自带背景色、图标左对齐，与条目融成一个「窗口菜单」。
Menu {
    id: root

    // 条目自带内边距，菜单内衬只留一条细边。
    padding: 6

    background: Item {
        implicitWidth: 200
        implicitHeight: 40

        // 软阴影两层：background 不裁剪子项，向四周外扩（下拉投影，
        // 光源在上方）。浅色主题的 overlayBg 近白，阴影自然更淡。
        Rectangle {
            x: -8
            y: -6
            width: parent.width + 16
            height: parent.height + 16
            radius: theme.radiusOverlay + 8
            color: theme.alpha(theme.overlayBg, 0.14)
        }
        Rectangle {
            x: -3
            y: -2
            width: parent.width + 6
            height: parent.height + 6
            radius: theme.radiusOverlay + 3
            color: theme.alpha(theme.overlayBg, 0.18)
        }

        // 玻璃底：半透明——下层内容以低对比透射上来。深色主题密度略高
        // （浅底下文字更依赖底色衬底），浅色主题下透射几乎不可见属预期。
        Rectangle {
            anchors.fill: parent
            radius: theme.radiusOverlay
            color: theme.alpha(theme.surfaceBg,
                               theme.variant === "dark" ? 0.90 : 0.95)
            border.color: theme.borderSubtle
            border.width: 1
        }

        // 顶部反光纱：上半区一层极淡的纵向渐变，玻璃的反光感。
        Rectangle {
            anchors.fill: parent
            radius: theme.radiusOverlay
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: theme.alpha(theme.textOnAccent, 0.05)
                }
                GradientStop {
                    position: 0.4
                    color: theme.alpha(theme.textOnAccent, 0.0)
                }
            }
        }

        // 内缘 1px 高光：玻璃厚度感。浅色主题白高光不可见，留空即可。
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(0, theme.radiusOverlay - 1)
            color: "transparent"
            border.width: 1
            border.color: theme.variant === "dark"
                          ? theme.alpha(theme.textOnAccent, 0.08)
                          : "transparent"
        }
    }

    // 进场：轻淡入 + 微展开；退场只淡出，更快更干净。注意 `opened` 要等
    // enter transition 跑完才置位（Popup 的状态机设计），测试断言要用
    // QTRY 而不是立即比较。
    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: theme.durationFast
        }
        NumberAnimation {
            property: "scale"
            from: 0.95
            to: 1
            duration: theme.durationFast
            easing.type: Easing.OutCubic
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
