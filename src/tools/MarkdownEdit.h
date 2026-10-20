#ifndef AWB_TOOLS_MARKDOWNEDIT_H
#define AWB_TOOLS_MARKDOWNEDIT_H

#include "tools/MarkdownHighlighter.h"

#include <QObject>
#include <QPointer>
#include <QVariantMap>

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

    // 选区加斜体（单个 *）；空选区时插入 ** 并把光标放到中间
    Q_INVOKABLE void applyItalic(QObject *textArea);

    // 选区加删除线（~~）；空选区时插入 ~~~~ 并把光标放到中间
    Q_INVOKABLE void applyStrike(QObject *textArea);

    // 选区加 <u></u>（markdown 没有下划线标记，窄白名单 HTML 是唯一可移植
    // 写法）；空选区时插入空元素并把光标放到中间
    Q_INVOKABLE void applyUnderline(QObject *textArea);

    // 行首切换 #/##/###；该行已是标题时改为目标级（不叠加），目标级与
    // 当前级相同时去掉标题（再点一次回到普通行）
    Q_INVOKABLE void applyHeading(QObject *textArea, int level);

    // 选区每行加 "1. " 递增编号；已有编号的行重编号；全部已编号时去掉
    Q_INVOKABLE void applyNumbered(QObject *textArea);

    // 选区每行加 "> "（空行保持空行）；已全部是引用时去掉
    Q_INVOKABLE void applyQuote(QObject *textArea);

    // 有选区：[选区](url) 且光标停在 url 处；无选区：[](url) 光标停在方括号中间
    Q_INVOKABLE void applyLink(QObject *textArea);

    // 围栏代码块并标注语言：按整行围栏（空选区在空行上插围栏骨架，
    // 光标停在两道围栏之间）；与 applyCode 的行内形态分工见 .cpp
    Q_INVOKABLE void applyFence(QObject *textArea, const QString &language);

    // 在光标处插入表格骨架：表头行 + 分隔行 + 一行示例（列数可指定，默认 3）
    Q_INVOKABLE void insertTable(QObject *textArea, int columns);

    // 在光标处插入分隔线（前后空行规范化：不与正文粘连）
    Q_INVOKABLE void insertDivider(QObject *textArea);

    // 选区每行加 "- [ ] "（agent 提示词的验收清单形态）；无选区时作用于当前行
    Q_INVOKABLE void applyTask(QObject *textArea);

    // Tab 键的编辑器行为（VSCode 语义）：多行选区给覆盖到的每行行首插
    // 一个 Tab（选区随之扩展）；无选区或单行选区保持原生——插入一个
    // Tab（单行选区被替换）。QML 侧经 Keys.onTabPressed 接入并吞掉默认
    // 的「选区替换为 Tab」。
    Q_INVOKABLE void handleTab(QObject *textArea);

    // Tab 的反向动作（Shift+Tab）：覆盖到的每行行首去掉一个缩进单位
    // （一个 Tab，或至多 4 个空格）；无选区时作用于光标所在行。整段都
    // 没有缩进时原样返回，不产生撤销步。QML 侧经 Keys.onBacktabPressed
    // 接入。
    Q_INVOKABLE void handleBacktab(QObject *textArea);

    // --- M3 查询侧（B1 约束：没有查询接口的动作不许设 checkable，这里
    //     就是 Ribbon checkable 按钮的数据源） ---

    // 光标处是否处于加粗 / 斜体 / 删除线 / 行内代码内（选中区间里任一
    // 位置命中即真；空选区查光标左侧字符的格式）
    Q_INVOKABLE bool isBoldAt(QObject *textArea) const;
    Q_INVOKABLE bool isItalicAt(QObject *textArea) const;
    Q_INVOKABLE bool isStrikeAt(QObject *textArea) const;
    Q_INVOKABLE bool isUnderlineAt(QObject *textArea) const;
    Q_INVOKABLE bool isCodeAt(QObject *textArea) const;
    // 光标所在行的标题级数：0 = 不是标题
    Q_INVOKABLE int headingLevelAt(QObject *textArea) const;

    // 纯光标操作（测试入口）：QTextCursor 的选区就是作用范围，调用后
    // cursor 的位置/选区即动作结果
    static void applyBoldToCursor(QTextCursor &cursor);
    static void applyItalicToCursor(QTextCursor &cursor);
    static void applyStrikeToCursor(QTextCursor &cursor);
    static void applyUnderlineToCursor(QTextCursor &cursor);
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
    static void handleTabToCursor(QTextCursor &cursor);
    static void handleBacktabToCursor(QTextCursor &cursor);

    // 纯查询（测试入口）：不修改文档
    static bool isBoldAtCursor(const QTextCursor &cursor);
    static bool isItalicAtCursor(const QTextCursor &cursor);
    static bool isStrikeAtCursor(const QTextCursor &cursor);
    static bool isUnderlineAtCursor(const QTextCursor &cursor);
    static bool isCodeAtCursor(const QTextCursor &cursor);
    static int headingLevelAtCursor(const QTextCursor &cursor);

    // --- B2 查找 / 替换（纯文档操作，与格式动作同级） -------------------

    /// 一次查找的结果：命中区间 + 是否发生回卷（从尾部跳回头部）。
    struct FindResult {
        int start = -1;          ///< 命中起点；-1 = 没找到
        int length = 0;          ///< 命中长度
        bool wrapped = false;    ///< 这一次查找发生了回卷
    };

    // QML 侧的查找/替换入口：编辑器（TextArea）+ 关键词 + 起点由调用方
    // 给；返回 QVariantMap（{start, length, wrapped}）——结构体不经
    // QMetaType 注册，map 是 QML 可消费的形状。大小写/全词取 tools 单例
    // 的偏好（调用方带入），保持纯层的参数化。
    Q_INVOKABLE QVariantMap find(QObject *textArea, const QString &needle,
                                 int from, bool caseSensitive,
                                 bool wholeWords) const;
    Q_INVOKABLE QVariantMap replaceAndFindNext(
        QObject *textArea, const QString &replacement,
        const QString &needle, bool caseSensitive, bool wholeWords) const;
    Q_INVOKABLE int replaceAll(QObject *textArea, const QString &needle,
                               const QString &replacement,
                               bool caseSensitive, bool wholeWords) const;

    // 从 from 向后查找 needle（到尾部回卷一次）。大小写/全词由参数表达，
    // 持久化偏好由调用方（tools.json 的 editor 段）带着进来。
    static FindResult findFrom(const QTextDocument *document,
                               const QString &needle, int from,
                               bool caseSensitive, bool wholeWords);

    // 替换「当前选中区间的这次命中」并自动找下一个。返回下一个命中的
    // FindResult（选中它：调用方把选区写回编辑器）。找不到下一个时
    // start = -1 且不回卷标志（替换动作结束）。
    static FindResult replaceAndFindNext(QTextDocument *document,
                                         const QTextCursor &selection,
                                         const QString &replacement,
                                         const QString &needle,
                                         bool caseSensitive,
                                         bool wholeWords);

    // 全部替换：返回替换次数（0 = 没有命中）。逐个在一个编辑块内完成，
    // 一次撤销全部回退。
    static int replaceAll(QTextDocument *document, const QString &needle,
                          const QString &replacement, bool caseSensitive,
                          bool wholeWords);

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
