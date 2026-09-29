import QtQuick
import QtQuick.Controls
import AgentWorkbench
import AgentWorkbench.App

// 全局滚动条：常驻显示、走主题令牌。Basic 样式自带的滚动条是 6px 宽的
// 临时条——停止滚动即淡出，页面等于没有任何可滚动的提示（用户反馈就是
// "这页滚不了"）。本组件在内容溢出时始终保持可见。
//
// 用法：`ScrollView { ScrollBar.vertical: AScrollBar {} }`，或同样附加在
// 裸 Flickable 上（侧栏导航、文件树 TreeView）。
ScrollBar {
    id: control

    // 常驻：样式模板把 visible 绑定到"正在滚动"，这里改为只要内容溢出
    // 就显示（实例级绑定会覆盖样式模板的）。框架也会读这个值：不可见的
    // 滚动条不占用 ScrollView 的 availableWidth。
    visible: size < 1.0

    // 内容很长时保证把手仍可抓取。
    minimumSize: 0.15

    // 自定位：框架只给附加在裸 Flickable 上的滚动条排几何（qquickscrollbar.cpp
    // 的 layoutVertical）；替换掉 Basic ScrollView 样式自带 ScrollBar.vertical
    // 声明的滚动条没有任何人给它定位——样式里的 parent/x/height 属于被替换的
    // 那条声明——会退化成一个不可见的 12x12 孤儿。这里复刻 Basic
    // ScrollView.qml 的定位。`parent` 在附加时才被赋值（目标 ScrollView 或
    // Flickable），绑定随之重算；Flickable 场景下数值与框架自排的一致。
    height: parent ? parent.height : implicitHeight
    x: parent ? parent.width - width : 0

    contentItem: Rectangle {
        implicitWidth: 8
        implicitHeight: 8
        radius: width / 2
        color: control.pressed ? theme.pressed(theme.scrollbar)
                               : (control.hovered ? theme.hover(theme.scrollbar)
                                                  : theme.scrollbar)
    }
}
