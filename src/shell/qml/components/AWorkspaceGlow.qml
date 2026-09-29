import QtQuick
import AgentWorkbench.App

// 页面底衬光斑：workspaceBg 是纯色平底，卡片改半透明玻璃后若无底衬，
// 「透」在视觉上无从体现。两团极低透明度的大径向光斑（accent 与 agent
// 调色板暖色）静态铺在页面最底层，只在主题变化或尺寸变化时重绘；透明度
// 压到不影响任何文字对比度的程度。
//
// 契约：纯展示、不接输入、无动效。宿主页面把它 anchors.fill 到根上并
// **先于内容声明**（声明顺序即层叠顺序，垫在最底）。
Canvas {
    id: root

    // 把主题令牌读进本地属性作为重绘触发器：Canvas 不会自动跟踪
    // theme，直接依赖才能在切换主题时重画
    property color tintA: theme.accent
    property color tintB: theme.agentPalette.length > 1 ? theme.agentPalette[1]
                                                        : theme.accent

    onTintAChanged: requestPaint()
    onTintBChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.clearRect(0, 0, width, height);
        var reach = Math.max(width, height) * 0.75;

        var g1 = ctx.createRadialGradient(width * 0.88, height * 0.06, 0,
                                          width * 0.88, height * 0.06, reach);
        g1.addColorStop(0, theme.alpha(tintA, 0.07));
        g1.addColorStop(1, theme.alpha(tintA, 0));
        ctx.fillStyle = g1;
        ctx.fillRect(0, 0, width, height);

        var g2 = ctx.createRadialGradient(width * 0.06, height * 0.95, 0,
                                          width * 0.06, height * 0.95, reach * 0.8);
        g2.addColorStop(0, theme.alpha(tintB, 0.05));
        g2.addColorStop(1, theme.alpha(tintB, 0));
        ctx.fillStyle = g2;
        ctx.fillRect(0, 0, width, height);
    }
}
