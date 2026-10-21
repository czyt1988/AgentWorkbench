#ifndef AWB_MARKDOWNEDIT_MARKDOWNHIGHLIGHTER_H
#define AWB_MARKDOWNEDIT_MARKDOWNHIGHLIGHTER_H

#include <QColor>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

class QString;
class QTextDocument;

namespace awb::markdownedit {

/// 判定 needle 是否命中 text 的 at 处（大小写按参数折叠、全词按两侧是否
/// 为单词字符判定）。findFrom / 匹配计数 / 查找高亮共用这一处语义；
/// 文本边缘（块边界、文档首尾）视作非单词字符边界。
bool needleMatchesAt(const QString &text, const QString &needle, int at,
                     bool caseSensitive, bool wholeWords);

/// Markdown 高亮的配色令牌：颜色由 MarkdownEdit 从当前 Theme 取出灌入，
/// 高亮器自身不认识 Theme——测试可以直接构造一组颜色验证规则。
struct MarkdownColors
{
    QColor heading;      ///< ATX 标题行（# ~ ######）的颜色
    QColor emphasis;     ///< 粗体 / 斜体 / 删除线标记对的颜色
    QColor code;         ///< 行内代码 `code` 的颜色
    QColor fence;        ///< ``` 围栏行的颜色
    QColor codeBlock;    ///< 围栏内部内容的颜色
    QColor quote;        ///< > 引用行的颜色
    QColor listMarker;   ///< 列表标记（- * + 1.）的颜色
    QColor link;         ///< [text](url) 链接的颜色
    QColor divider;      ///< 水平分隔线 --- 的颜色
    QColor search;       ///< 查找命中的背景色（带透明度，叠在语法前景色之下）
};

/// 轻量 Markdown 语法高亮器：逐块状态机 + 正则规则。
///
/// 只覆盖日常 markdown 编辑需要的子集：围栏代码块（跨行状态）、标题、
/// 分隔线、
/// 引用、列表标记，以及行内的代码 / 粗体 / 斜体 / 删除线 / 链接。规则
/// 刻意保持简单，不追求 CommonMark 全量正确（setext 标题、缩进代码块、
/// 嵌套列表的严格语义不在目标内）。配色经 setColors 注入，主题切换后
/// 由调用方负责 rehighlight()。
class MarkdownHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    MarkdownHighlighter(QTextDocument *document, const MarkdownColors &colors);

    // 替换配色；不自动重扫，调用方随后 rehighlight()
    void setColors(const MarkdownColors &colors);

    // 当前配色
    MarkdownColors colors() const;

    // 设置查找高亮的关键词（空串 = 清除查找高亮）；立即重扫全文
    void setSearch(const QString &needle, bool caseSensitive,
                   bool wholeWords);

protected:
    void highlightBlock(const QString &text) override;

private:
    // 语法规则主体（highlightBlock 拆出的第一段；结果写进格式网格）
    void highlightSyntax(const QString &text,
                         QVector<QTextCharFormat> &grid);

    // 查找命中叠背景色（在语法规则之后合并进网格——setFormat 是逐字符
    // 覆盖，直接写会盖掉语法前景色，所以先在网格里合并再统一刷出）
    void applySearchHighlight(const QString &text,
                              QVector<QTextCharFormat> &grid);

    MarkdownColors m_colors;
    QString m_searchNeedle;              ///< 查找关键词（空 = 无查找高亮）
    bool m_searchCaseSensitive = false;  ///< 查找区分大小写
    bool m_searchWholeWords = false;     ///< 查找全词匹配
};

} // namespace awb::markdownedit

#endif // AWB_MARKDOWNEDIT_MARKDOWNHIGHLIGHTER_H
