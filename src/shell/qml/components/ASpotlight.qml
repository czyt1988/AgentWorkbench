import QtQuick
import AgentWorkbench.App

// 指针聚光覆盖层：复刻 Web 端磨砂玻璃卡的「聚光光斑 + 描边流光」——以
// (spotX, spotY) 为圆心的径向光斑被圆角卡片路径裁剪，同一圆心的径向渐变
// 再沿卡片边缘描出一圈流光，两者随 active 一起淡入淡出。
//
// 契约：宿主提供悬停判据（active）与指针坐标（spotX/spotY，宿主坐标系）。
// 本组件纯展示、不接收任何输入事件、也不自带 hover 探测——本仓库的 hover
// 是独占投递，卡内 hoverEnabled 的 MouseArea 与 Control 会吞掉根部的
// HoverHandler，判据必须由宿主统一合并（写法见 SkillCard），组件内探测
// 只会得到闪断的假信号。
//
// 实现选 Canvas 2D 径向渐变：Qt 5.15 与 Qt 6 内建且 API 同形，避免引入
// QtGraphicalEffects / Qt5Compat 这类两版异名的模块。淡入淡出走 opacity
// 动画（合成器免费），只有指针移动才触发重绘。
Item {
    id: root

    // 卡片圆角半径：光斑裁剪与描边环的路径都跟随它
    property real radius: theme.radiusCard
    // 光的颜色：通常注入 agent 的语义色
    property color accentColor: theme.accent
    // 点亮状态：由宿主绑定到合并后的悬停判据
    property bool active: false
    // 指针位置（宿主坐标系）。悬停丢失期间保持上一次的值，因此指针移到
    // 会吞 hover 的卡内子项上时光斑停在原地，而不是跳回默认位置
    property real spotX: width / 2
    property real spotY: height / 2

    // 淡入淡出强度（0..1）：只驱动 Canvas 的 opacity，不触发重绘
    property real intensity: active ? 1 : 0
    Behavior on intensity {
        NumberAnimation { duration: theme.durationNormal }
    }

    // 光斑/流光的渐变半径随卡片尺寸缩放（参照实现：232px 宽的卡配 300px
    // 光斑与 260px 描边渐变）
    readonly property real spotRadius: Math.max(width, height) * 1.3
    readonly property real ringRadius: Math.max(width, height) * 1.12

    visible: intensity > 0.01

    onActiveChanged: if (active) { canvas.requestPaint() }
    onSpotXChanged: canvas.requestPaint()
    onSpotYChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onAccentColorChanged: canvas.requestPaint()
    onRadiusChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        opacity: root.intensity

        // 圆角矩形路径；inset 让路径从边缘向内收，描边时外半边不落卡外
        function roundedRect(ctx, inset, r) {
            var w = width - 2 * inset;
            var h = height - 2 * inset;
            var rr = Math.max(0, Math.min(r - inset, Math.min(w, h) / 2));
            ctx.beginPath();
            ctx.moveTo(inset + rr, inset);
            ctx.lineTo(inset + w - rr, inset);
            ctx.arcTo(inset + w, inset, inset + w, inset + rr, rr);
            ctx.lineTo(inset + w, inset + h - rr);
            ctx.arcTo(inset + w, inset + h, inset + w - rr, inset + h, rr);
            ctx.lineTo(inset + rr, inset + h);
            ctx.arcTo(inset, inset + h, inset, inset + h - rr, rr);
            ctx.lineTo(inset, inset + rr);
            ctx.arcTo(inset, inset, inset + rr, inset, rr);
            ctx.closePath();
        }

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);

            // 聚光光斑：参照实现的 ::before（24% 场景色 → 65% 处透明）。
            // theme.alpha 返回的 QColor 可直接作 color stop——Canvas 接受
            // 颜色值并保留 alpha 通道
            roundedRect(ctx, 0, root.radius);
            var spot = ctx.createRadialGradient(root.spotX, root.spotY, 0,
                                                root.spotX, root.spotY, root.spotRadius);
            spot.addColorStop(0, theme.alpha(root.accentColor, 0.22));
            spot.addColorStop(0.65, theme.alpha(root.accentColor, 0));
            ctx.fillStyle = spot;
            ctx.fill();

            // 描边流光：参照实现的 ::after（同源径向渐变经 mask 只留 1px
            // 边框环）；这里直接沿内缩 1px 的圆角路径描 2px，等价于取环
            roundedRect(ctx, 1, root.radius);
            var ring = ctx.createRadialGradient(root.spotX, root.spotY, 0,
                                                root.spotX, root.spotY, root.ringRadius);
            ring.addColorStop(0, theme.alpha(root.accentColor, 0.75));
            ring.addColorStop(0.62, theme.alpha(root.accentColor, 0));
            ctx.strokeStyle = ring;
            ctx.lineWidth = 2;
            ctx.stroke();
        }
    }
}
