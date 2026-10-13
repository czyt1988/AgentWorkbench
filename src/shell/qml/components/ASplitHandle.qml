import QtQuick
import AgentWorkbench
import AgentWorkbench.App

// 可拖的分隔条：一条细的视觉条 + 一圈更宽的命中区（4px 的条要 12px 的
// 命中区才抓得住），指针停上去提亮成 borderStrong。
//
// 组件只负责「看起来像分隔条、被拖时把指针位置报出去」。位置代表什么
// （左栏的像素宽度、编辑/预览的比例刻度）、拖到什么程度算越界（收起、
// 夹下限），全是宿主的语义，宿主在 onDragged 里自己换算——同一个组件
// 因此能服务两种完全不同的拖拽语义。
//
// 坐标一律换算到 host 再上报。指针的绝对位置是这里唯一稳定的量：相对
// 增量会被手柄自身随拖动产生的位移吃掉（左栏手柄早前逐事件读 mouse.x
// 再减去面板当前宽度，两项相减恒等于当前宽度，拖动成了无操作；减
// leftPanel.width 更是把任何拖动都算成负宽度、直接收起）。
Item {
    id: handle

    ///< 视觉条粗细
    property real visualWidth: 4
    ///< 命中区宽度（比视觉条宽，指针不必精确压在那几个像素上）
    property real hitWidth: 12
    ///< 视觉条圆角；0 = 方头（与两栏转角齐平的场合）
    property real barRadius: 0
    ///< 拖拽坐标换算的目标：宿主「值的坐标系」，通常承载被分隔的两栏
    property Item host: null
    ///< 是否吃指针事件（宿主用它表达「此刻不可拖」，如只剩一栏时）
    property bool interactive: true

    ///< 指针是否停在命中区上（宿主可据此联动自己的视觉）
    readonly property bool hovered: hit.containsMouse

    ///< 按下：x/y 是 host 坐标系里的按下点
    signal dragStarted(real x, real y)
    ///< 拖动中：x/y 是 host 坐标系里的当前点，仅在按住时发
    signal dragged(real x, real y)

    implicitWidth: hitWidth

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        x: (handle.width - handle.visualWidth) / 2
        width: handle.visualWidth
        height: handle.height
        radius: handle.barRadius
        color: handle.hovered ? theme.borderStrong : theme.separator
    }

    MouseArea {
        id: hit

        // 命中区不得窄于 hitWidth：宿主把组件本体放在 4px 宽的槽位里时
        // （编辑/预览的分隔条就是这样），命中区照旧向两侧溢出 4px。
        width: Math.max(handle.hitWidth, handle.width)
        height: parent.height
        x: (parent.width - width) / 2
        cursorShape: Qt.SplitHCursor
        hoverEnabled: true
        enabled: handle.interactive

        onPressed: function(mouse) { handle.report(true, mouse) }
        onPositionChanged: function(mouse) {
            if (pressed)
                handle.report(false, mouse)
        }
    }

    // 换算的起点必须是命中区而不是本组件：mouse.x 是命中区的局部坐标，
    // 而命中区在窄槽位里会相对根偏出去（4px 的槽位里偏 -4px）。从根出发
    // 换算会整体错掉这个偏移量。
    // host 为空时不换算，直接报局部坐标——那是「宿主就是本组件的父级
    // 坐标系」这一最常见情形的等价说法。
    function report(start, mouse) {
        const at = host ? hit.mapToItem(host, mouse.x, mouse.y)
                        : Qt.point(mouse.x, mouse.y)
        if (start)
            dragStarted(at.x, at.y)
        else
            dragged(at.x, at.y)
    }
}
