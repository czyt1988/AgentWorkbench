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

    // --- M3 新增格式动作（B1：全部沿用静态光标操作形状，一次撤销一步） ---

    // 行首切换 #/##/###；该行已是标题时改为目标级（不叠加），目标级与
    // 当前级相同时去掉标题（再点一次回到普通行）
    Q_INVOKABLE void applyHeading(QObject *textArea, int level);

    // 选区每行加 "1. " 递增编号；已有编号的行重编号；全部已编号时去掉
    Q_INVOKABLE void applyNumbered(QObject *textArea);

    // 选区每行加 "> "（空行保持空行）；已全部是引用时去掉
    Q_INVOKABLE void applyQuote(QObject *textArea);

    // 有选区：[选区](url) 且光标停在 url 处；无选区：[](url) 光标停在方括号中间
    Q_INVOKABLE void applyLink(QObject *textArea);

    // 围栏代码块并标注语言（applyCode 的换行围栏分支 + 语言标注；空语言
    // 与 applyCode 输出一致）
    Q_INVOKABLE void applyFence(QObject *textArea, const QString &language);

    // 在光标处插入表格骨架：表头行 + 分隔行 + 一行示例（列数可指定，默认 3）
    Q_INVOKABLE void insertTable(QObject *textArea, int columns);

    // 在光标处插入分隔线（前后空行规范化：不与正文粘连）
    Q_INVOKABLE void insertDivider(QObject *textArea);

    // 选区每行加 "- [ ] "（agent 提示词的验收清单形态）；无选区时作用于当前行
    Q_INVOKABLE void applyTask(QObject *textArea);

    // --- M3 查询侧（B1 约束：没有查询接口的动作不许设 checkable，这里
    //     就是 Ribbon checkable 按钮的数据源） ---

    // 光标处是否处于加粗 / 斜体 / 删除线 / 行内代码内（选中区间里任一
    // 位置命中即真；空选区查光标左侧字符的格式）
    Q_INVOKABLE bool isBoldAt(QObject *textArea) const;
    Q_INVOKABLE bool isItalicAt(QObject *textArea) const;
    Q_INVOKABLE bool isStrikeAt(QObject *textArea) const;
    Q_INVOKABLE bool isCodeAt(QObject *textArea) const;
    // 光标所在行的标题级数：0 = 不是标题
    Q_INVOKABLE int headingLevelAt(QObject *textArea) const;

    // 纯光标操作（测试入口）：QTextCursor 的选区就是作用范围，调用后
    // cursor 的位置/选区即动作结果
    static void applyBoldToCursor(QTextCursor &cursor);
    static void applyCodeToCursor(QTextCursor &cursor);
    static void applyBulletToCursor(QTextCursor &cursor);
    static void applyHeadingToCursor(QTextCursor &cursor, int level);
    static void applyNumberedToCursor(QTextCursor &cursor);
    static void applyQuoteToCursor(QTextCursor &cursor);
    static void applyLinkToCursor(QTextCursor &cursor);
    static void applyFenceToCursor(QTextCursor &cursor,
                                   const QString &language);
    static void insertTableToCursor(QTextCursor &cursor, int columns);
    static void insertDividerToCursor(QTextCursor &cursor);
    static void applyTaskToCursor(QTextCursor &cursor);

    // 纯查询（测试入口）：不修改文档
    static bool isBoldAtCursor(const QTextCursor &cursor);
    static bool isItalicAtCursor(const QTextCursor &cursor);
    static bool isStrikeAtCursor(const QTextCursor &cursor);
    static bool isCodeAtCursor(const QTextCursor &cursor);
    static int headingLevelAtCursor(const QTextCursor &cursor);

private:
    // 从 Theme 提取高亮配色
    static MarkdownColors colorsFromTheme(const theme::Theme *theme);

    // 通用路径：取编辑器的文档与选区 -> 跑静态操作 -> 把结果选区写回
    void editTextArea(QObject *textArea, void (*operation)(QTextCursor &));

    // 带参操作的通用路径（heading/fence 带一个字符串/整数参数的重载）
    void editTextArea(QObject *textArea,
                      void (*operation)(QTextCursor &, int), int number);
    void editTextArea(QObject *textArea,
                      void (*operation)(QTextCursor &, const QString &),
                      const QString &text);

    // 查询的通用路径：取编辑器的文档与选区，跑静态查询，返回布尔/整数
    bool queryTextAreaBool(QObject *textArea,
                           bool (*query)(const QTextCursor &)) const;
    int queryTextAreaInt(QObject *textArea,
                         int (*query)(const QTextCursor &)) const;

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
