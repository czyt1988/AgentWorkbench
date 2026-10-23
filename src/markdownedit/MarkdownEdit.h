#ifndef AWB_MARKDOWNEDIT_MARKDOWNEDIT_H
#define AWB_MARKDOWNEDIT_MARKDOWNEDIT_H

#include "markdownedit/MarkdownHighlighter.h"

#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include <QList>

class QTextCursor;

namespace awb::theme {
class Theme;
} // namespace awb::theme

namespace awb::markdownedit {

/// Tab / Shift+Tab 的缩进偏好：值来自宿主应用的设置，纯层不读配置文件
///（可复用模块只依赖 theme，参数一律由调用方带入）。
struct IndentOptions
{
    bool useSpaces = true;     ///< true = 插空格；false = 插 \t
    int spacesPerIndent = 2;   ///< 每级缩进的空格数（useSpaces 时生效）
};

/// QML 单例：markdown 编辑支持入口——attach 给编辑器（TextArea）挂语法
/// 高亮，applyBold/applyCode 等是右键菜单与命令条调用的格式化动作。
/// 本应用把它注册为 AgentWorkbench.App 上的 MarkdownEdit；其它项目按
/// 自己的 URI 注册即可（见 docs 的集成说明）。
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

    // 格式动作是**切换**语义（A5）：选区已被同名标记包裹、或光标正停在
    // 标记对内时，再执行一次 = 去掉标记；否则包裹/插入骨架。

    // 切换加粗：选区加/去 **（无选区且不在加粗内时插入 **** 并把光标放
    // 到中间）
    Q_INVOKABLE void applyBold(QObject *textArea);

    // 切换行内代码：选区加/去标记；选区含换行时改用独立围栏代码块
    Q_INVOKABLE void applyCode(QObject *textArea);

    // 给选区覆盖到的每一行行首加 "- "（无选区时作用于光标所在行）
    Q_INVOKABLE void applyBullet(QObject *textArea);

    // --- M3 新增格式动作（B1：全部沿用静态光标操作形状，一次撤销一步） ---

    // 选区加/去斜体（单个 *）；空选区时插入 ** 并把光标放到中间
    Q_INVOKABLE void applyItalic(QObject *textArea);

    // 选区加/去删除线（~~）；空选区时插入 ~~~~ 并把光标放到中间
    Q_INVOKABLE void applyStrike(QObject *textArea);

    // 选区加/去 <u></u>（markdown 没有下划线标记，窄白名单 HTML 是唯一可
    // 移植写法）；空选区时插入空元素并把光标放到中间
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

    // 在指定位置插入一段 Markdown 文本；position < 0 时替换当前选区，否则
    // 把光标移到该位置后插入。整段一次插入，只产生一步撤销
    Q_INVOKABLE bool insertMarkdown(QObject *textArea, int position,
                                    const QString &text);

    // 选区每行加 "- [ ] "（agent 提示词的验收清单形态）；无选区时作用于当前行
    Q_INVOKABLE void applyTask(QObject *textArea);

    // Tab 键的编辑器行为（VSCode 语义）：多行选区给覆盖到的每行行首插一
    // 个缩进单位（选区随之扩展）；无选区或单行选区保持原生——插入一个
    // 缩进单位（单行选区被替换）。缩进单位由 options 决定（空格 × 宽度
    // 或字面 \t）。QML 侧经 Keys.onTabPressed 接入并吞掉默认的「选区替换
    // 为 Tab」。
    Q_INVOKABLE void handleTab(QObject *textArea, bool useSpaces,
                               int spacesPerIndent);

    // Tab 的反向动作（Shift+Tab）：覆盖到的每行行首去掉一个缩进单位
    //（一个 Tab，或至多 spacesPerIndent 个空格）；无选区时作用于光标所
    // 在行。整段都没有缩进时原样返回，不产生撤销步。QML 侧经
    // Keys.onBacktabPressed 接入。
    Q_INVOKABLE void handleBacktab(QObject *textArea, bool useSpaces,
                                   int spacesPerIndent);

    // --- 回车键的列表延续（A3「输入手感」） -------------------------------

    // 回车延续列表 / 编号 / 引用 / 任务项：光标（无选区）在带前缀的行上
    // 时，插入换行并带上前缀（编号递增、任务项回到未勾选）；行内容为空
    //（只剩前缀）时退出该结构——删掉前缀只留换行。返回是否接管了这次回
    // 车（false = 调用方放行默认行为）。QML 侧经 Keys.onReturnPressed /
    // Keys.onEnterPressed 接入。
    Q_INVOKABLE bool handleEnter(QObject *textArea);

    // --- 输入时的标记配对（A6「输入手感」） -------------------------------

    // 输入单个字符时的自动配对：`*`/反引号/`~` 插入成对标记并把光标放到
    // 中间（空对里再按同一字符升级成双标记，如 * → **；~~ 的删除线骨架
    // 同理）；`[`/`(` 插入对称括号；光标后正好是同一闭合字符时跳过而不插
    // 入。返回是否接管（false = 调用方放行默认输入）。QML 侧经
    // Keys.onPressed 的单字符分支接入。
    Q_INVOKABLE bool autoType(QObject *textArea, const QString &typed);

    // --- 行与块操作（B 批次：同一静态光标操作形状） -----------------------

    /// convertCase 的目标形态（Q_ENUM 后 QML 可写 MarkdownEdit.UpperCase 等）。
    enum CaseMode {
        UpperCase = 0,  ///< 全大写
        LowerCase = 1,  ///< 全小写
        TitleCase = 2,  ///< 词首大写、其余小写
    };
    Q_ENUM(CaseMode)

    // 选区覆盖到的行（无选区 = 光标所在行）与上一行交换；已在文档顶时
    // 原样返回，不留空撤销步。选区跟着行走（端点保持段内相对位置）
    Q_INVOKABLE void moveLineUp(QObject *textArea);

    // 同上，与下一行交换；已在文档底时原样返回
    Q_INVOKABLE void moveLineDown(QObject *textArea);

    // 覆盖到的行在紧随其后复制一份（含行尾分隔符）；选区落在副本上
    Q_INVOKABLE void duplicateLine(QObject *textArea);

    // 删掉覆盖到的整行（含行尾分隔符），光标落到紧随其后的行行首
    Q_INVOKABLE void deleteLine(QObject *textArea);

    // 选区扩展到覆盖到的整行（无选区 = 选中光标所在行；不含行尾换行）
    Q_INVOKABLE void selectLine(QObject *textArea);

    // 选中光标处的词（字母/数字/下划线连成的段）；光标在空白上或已有
    // 选区时原样返回
    Q_INVOKABLE void selectWord(QObject *textArea);

    // 选区文本的大小写转换（无选区时作用于光标处的词）；mode 取 CaseMode
    Q_INVOKABLE void convertCase(QObject *textArea, int mode);

    // 标题升降级（选区覆盖到的每个标题行）：升 = # 数减一，H1 再升 =
    // 去标题（循环语义与 applyHeading 同级再点一致）；降 = # 数加一，
    // 封顶 6。非标题行不动
    Q_INVOKABLE void promoteHeading(QObject *textArea);
    Q_INVOKABLE void demoteHeading(QObject *textArea);

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
    // 在光标处插入文本（有选区则替换）；返回是否真的插入了内容
    static bool insertMarkdownToCursor(QTextCursor &cursor,
                                       const QString &text);
    static void applyTaskToCursor(QTextCursor &cursor);
    static void handleTabToCursor(QTextCursor &cursor,
                                  const IndentOptions &options);
    static void handleBacktabToCursor(QTextCursor &cursor,
                                      const IndentOptions &options);
    // 回车延续（A3）：返回是否接管（false = 行上没有可延续的结构）
    static bool smartEnterToCursor(QTextCursor &cursor);
    // 输入配对（A6）：返回是否接管（false = 该字符不在配对表里或语义上
    // 应放行默认输入）
    static bool autoTypeToCursor(QTextCursor &cursor, const QString &typed);
    static void moveLineUpToCursor(QTextCursor &cursor);
    static void moveLineDownToCursor(QTextCursor &cursor);
    static void duplicateLineToCursor(QTextCursor &cursor);
    static void deleteLineToCursor(QTextCursor &cursor);
    static void selectLineToCursor(QTextCursor &cursor);
    static void selectWordToCursor(QTextCursor &cursor);
    static void convertCaseToCursor(QTextCursor &cursor, int mode);
    static void promoteHeadingToCursor(QTextCursor &cursor);
    static void demoteHeadingToCursor(QTextCursor &cursor);

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
    // QMetaType 注册，map 是 QML 可消费的形状。大小写/全词偏好由调用方
    // 带入（宿主应用自己持久化），保持纯层的参数化。
    Q_INVOKABLE QVariantMap find(QObject *textArea, const QString &needle,
                                 int from, bool caseSensitive,
                                 bool wholeWords) const;
    Q_INVOKABLE QVariantMap replaceAndFindNext(
        QObject *textArea, const QString &replacement,
        const QString &needle, bool caseSensitive, bool wholeWords) const;
    Q_INVOKABLE int replaceAll(QObject *textArea, const QString &needle,
                               const QString &replacement,
                               bool caseSensitive, bool wholeWords) const;

    // 关键词在编辑器全文的命中总数（「N/M」里的 M）
    Q_INVOKABLE int findCount(QObject *textArea, const QString &needle,
                              bool caseSensitive, bool wholeWords) const;

    // 从 from 起第一个命中的序号，1 起（「N/M」里的 N；0 = 没有命中）
    Q_INVOKABLE int findIndexOf(QObject *textArea, const QString &needle,
                                int from, bool caseSensitive,
                                bool wholeWords) const;

    // 给编辑器里全部命中叠背景高亮（经挂着的语法高亮器）；needle 为空
    // 时清除。关键词变更由调用方驱动，不依赖文档改动事件
    Q_INVOKABLE void setSearchHighlight(QObject *textArea,
                                        const QString &needle,
                                        bool caseSensitive,
                                        bool wholeWords);

    // 从 from 向后查找 needle（到尾部回卷一次）。大小写/全词由参数表达，
    // 持久化偏好由调用方带着进来（宿主应用自己决定存哪）。
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

    // 关键词在全文的命中总数（不重叠计数，与 replaceAll 口径一致）
    static int countMatches(const QTextDocument *document,
                            const QString &needle, bool caseSensitive,
                            bool wholeWords);

    // 从 from 起第一个命中的序号（1 起；0 = 没有命中）——「3/12」里的 3
    static int matchIndexAt(const QTextDocument *document,
                            const QString &needle, int from,
                            bool caseSensitive, bool wholeWords);

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
    // Tab/Shift+Tab 的通用路径（缩进偏好由调用方带入）
    void editTextArea(QObject *textArea,
                      void (*operation)(QTextCursor &, const IndentOptions &),
                      const IndentOptions &options);

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

} // namespace awb::markdownedit

#endif // AWB_MARKDOWNEDIT_MARKDOWNEDIT_H
