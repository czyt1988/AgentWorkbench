#ifndef AWB_MARKDOWN_MARKDOWNCHUNKER_H
#define AWB_MARKDOWN_MARKDOWNCHUNKER_H

#include <QVector>

namespace awb::markdown {

/// 一个顶层块：预览渲染与增量重渲染的最小单位。
///
/// 行号一律 0 基、闭区间，endLine 指向块的最后一行；text 不含行尾换行符。
/// 块与页面 DOM 的 `<section>` 一一对应，所以 startLine/endLine 也是
/// 「编辑器行号 → 预览块」双向定位的锚点。
struct MarkdownBlock
{
    int index = 0;        ///< 在块序列中的位置
    int startLine = 0;    ///< 首行（含）
    int endLine = 0;      ///< 末行（含）
    QString text;         ///< 块原文
    QString hash;         ///< text 的短摘要，增量比对的键
};

/// 把 Markdown 源码切成顶层块，并算出两次切分之间的最小替换区间。
///
/// 存在的理由是大文档的性能：整篇一次 innerHTML 挂载会让首屏和滚动都付全篇
/// 代价，按块挂载才能配合 CSS `content-visibility` 跳过屏外布局、也才能只重
/// 渲染改动的那几块。代价是「逐块渲染再拼接」必须与「整篇渲染」等价，块边界
/// 切错就会改变语义（把列表后面的段落吞进列表、把围栏里的空行当成分隔）。
/// 因此切分器是**行级状态机**而不是「按空行 split」：围栏、显示数学、setext
/// 标题下划线、松散列表的延续段、表格连续段都在这里显式处理，等价性由
/// tst_markdownchunker 用整个 docs/ 的 Markdown 做语料比对守住。
///
/// 只做切分，不解析、不渲染；纯函数、无 Qt GUI 依赖，Qt 5 / Qt 6 通用。
class MarkdownChunker
{
public:
    /// 一次差异的结果：页面从 startIndex 起删掉 removeCount 个 `<section>`，
    /// 再把 inserted 依次插到同一位置。区间之外的块一律不动。
    struct Diff
    {
        int startIndex = 0;              ///< 首个需要替换的块索引
        int removeCount = 0;             ///< 旧序列里被替换掉的块数
        QVector<MarkdownBlock> inserted; ///< 替换进来的新块（连续区间）

        // 没有任何块变化（removeCount 与 inserted 同时为空）
        bool isNull() const;
    };

    // 按顶层块切分 source；空文本得到空序列，不是含一个空块的序列
    static QVector<MarkdownBlock> split(const QString &source);

    // 比较两次 split 的结果，给出最小替换区间；无差异时返回 isNull() 的 Diff
    static Diff diff(const QVector<MarkdownBlock> &oldBlocks,
                     const QVector<MarkdownBlock> &newBlocks);

    // 文本的块键：sha1 前 16 位十六进制。公开是为了让测试能独立复算，
    // 也让页面缓存（按 hash 缓存渲染结果、缓存 mermaid 的 SVG）用同一个键。
    static QString blockHash(const QString &text);
};

} // namespace awb::markdown

#endif // AWB_MARKDOWN_MARKDOWNCHUNKER_H
