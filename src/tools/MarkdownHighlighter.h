#ifndef AWB_TOOLS_MARKDOWNHIGHLIGHTER_H
#define AWB_TOOLS_MARKDOWNHIGHLIGHTER_H

#include <QColor>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class QString;
class QTextDocument;

namespace awb::tools {

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
};

/// 轻量 Markdown 语法高亮器：逐块状态机 + 正则规则。
///
/// 只覆盖提示词编写台需要的子集：围栏代码块（跨行状态）、标题、分隔线、
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

protected:
    void highlightBlock(const QString &text) override;

private:
    MarkdownColors m_colors;
};

} // namespace awb::tools

#endif // AWB_TOOLS_MARKDOWNHIGHLIGHTER_H
