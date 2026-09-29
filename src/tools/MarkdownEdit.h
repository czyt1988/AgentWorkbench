#ifndef AWB_TOOLS_MARKDOWNEDIT_H
#define AWB_TOOLS_MARKDOWNEDIT_H

#include "tools/MarkdownHighlighter.h"

#include <QObject>
#include <QPointer>

#include <QList>

class QTextCursor;

namespace awb::theme {
class Theme;
} // namespace awb::theme

namespace awb::tools {

/// QML 单例（AgentWorkbench.App 上的 MarkdownEdit）：提示词编辑器的
/// markdown 支持入口——attach 给编辑器挂语法高亮，applyBold/applyCode/
/// applyBullet 是右键菜单工具栏调用的三个格式化动作。
///
/// 动作全部经 QTextCursor 的编辑块完成（一次撤销一步），具体逻辑是
/// 不依赖 QML 运行时的纯静态函数，测试直接拿 QTextDocument 驱动。
class MarkdownEdit : public QObject
{
    Q_OBJECT

public:
    explicit MarkdownEdit(theme::Theme *theme, QObject *parent = nullptr);

    // 把语法高亮挂到编辑器（TextArea）的 textDocument 上；同一文档重复调用无害
    Q_INVOKABLE void attach(QObject *textArea);

    // 给选区加 **（无选区时插入 **** 并把光标放到中间）
    Q_INVOKABLE void applyBold(QObject *textArea);

    // 给选区加行内代码；选区含换行时改用独立围栏代码块
    Q_INVOKABLE void applyCode(QObject *textArea);

    // 给选区覆盖到的每一行行首加 "- "（无选区时作用于光标所在行）
    Q_INVOKABLE void applyBullet(QObject *textArea);

    // 纯光标操作（测试入口）：QTextCursor 的选区就是作用范围，调用后
    // cursor 的位置/选区即动作结果
    static void applyBoldToCursor(QTextCursor &cursor);
    static void applyCodeToCursor(QTextCursor &cursor);
    static void applyBulletToCursor(QTextCursor &cursor);

private:
    // 从 Theme 提取高亮配色
    static MarkdownColors colorsFromTheme(const theme::Theme *theme);

    // 通用路径：取编辑器的文档与选区 -> 跑静态操作 -> 把结果选区写回
    void editTextArea(QObject *textArea, void (*operation)(QTextCursor &));

    // Theme::changed 的处理：刷新配色并让所有挂着的编辑器重扫
    void reapplyTheme();

    // 清掉文档已销毁的高亮器指针
    void pruneHighlighters();

    theme::Theme *m_theme;                         ///< 配色来源（不拥有）
    MarkdownColors m_colors;                       ///< 当前配色
    QList<QPointer<MarkdownHighlighter>> m_highlighters;  ///< 挂到各编辑器的高亮器（文档是父对象）
};

} // namespace awb::tools

#endif // AWB_TOOLS_MARKDOWNEDIT_H
