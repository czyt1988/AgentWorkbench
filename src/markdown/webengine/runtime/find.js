// 文档内搜索：自己数、自己跳，不用 Chromium 的原生查找。
//
// 为什么不用原生 Ctrl+F：预览页靠 CSS `content-visibility: auto` 跳过屏外
// 块的布局（这是大文档滚动不掉帧的主收益项），而原生查找对这种「在 DOM 里、
// 但引擎跳过渲染」的内容行为不受我们控制——命中数、当前第几个、跳过去的
// 滚动位置都拿不到，也没法用主题色画。自己实现换来的是：总数与序号是确定值、
// 高亮跟随主题。
//
// 两条已知的取舍，都是有意为之：
//  1. 只数**已挂载**的块。首屏之后解析是分片进行的，刚打开文档就搜时，还没
//     挂上的尾部不算命中；渲染完成后同一次搜索的命中数自然变全。
//  2. 跨行内标记的匹配找不到（`强**调查**研` 里搜「调查」）。要在 HTML 结构
//     上跨节点拼匹配串得先把整块文本展平再映射回 DOM，代价与复杂度都不划算，
//     而且这类位置在正文里极少。
//
// 高亮只落在**当前命中所在的那一块**：全篇插 <mark> 会让一次编辑退化成
// 20k 次 DOM 变更，而用户看得见的位置本来就只有当前这一块。

function countIn(text, needle, caseSensitive)
{
    if (!needle) {
        return 0;
    }
    const hay = caseSensitive ? text : text.toLowerCase();
    const goal = caseSensitive ? needle : needle.toLowerCase();
    let count = 0;
    let at = hay.indexOf(goal);
    while (at >= 0) {
        count += 1;
        at = hay.indexOf(goal, at + goal.length);
    }
    return count;
}

export function createFinder(options)
{
    let activeIndex = -1;         // 当前命中在全部命中里的下标（0 基）
    let markedElement = null;     // 插过 <mark> 的元素，下一次先还原

    function unwrapMarks()
    {
        if (!markedElement || !markedElement.isConnected) {
            markedElement = null;
            return;
        }
        markedElement.querySelectorAll(".awb-mark").forEach((mark) => {
            const parent = mark.parentNode;
            if (parent) {
                parent.replaceChild(
                    document.createTextNode(mark.textContent), mark);
            }
        });
        // normalize() 把拆开的相邻文本节点并回去：不并的话下一次搜索会在
        // 一堆碎片上重走一遍，跨节点的匹配就更找不到了。
        markedElement.normalize();
        markedElement = null;
    }

    /**
     * 在元素内把该关键词的每一处匹配包成 <mark>，并给第 goalIndex 处加上
     * 当前命中的样式。
     *
     * @return 是否真的标到了（元素里没有该匹配时返回 false，调用方据此收尾）
     */
    function markWithin(element, needle, caseSensitive, goalIndex)
    {
        const goal = caseSensitive ? needle : needle.toLowerCase();
        const walker = document.createTreeWalker(element, NodeFilter.SHOW_TEXT, {
            acceptNode(node) {
                if (!node.nodeValue || !node.nodeValue.trim()) {
                    return NodeFilter.FILTER_REJECT;
                }
                // <mark> 内部不再进：unwrapMarks() 已经清过，这里是双保险
                if (node.parentElement && node.parentElement.closest(".awb-mark")) {
                    return NodeFilter.FILTER_REJECT;
                }
                return NodeFilter.FILTER_ACCEPT;
            }
        });

        // 先收集再动手：包 <mark> 会把文本节点一分为二，边走边改会让
        // TreeWalker 的当前位置失效，后面的匹配就漏了。
        const hits = [];
        let node = walker.nextNode();
        while (node) {
            const hay = caseSensitive ? node.nodeValue : node.nodeValue.toLowerCase();
            let at = hay.indexOf(goal);
            while (at >= 0) {
                hits.push({node: node, at: at});
                at = hay.indexOf(goal, at + goal.length);
            }
            node = walker.nextNode();
        }
        if (!hits.length) {
            return false;
        }
        // 倒着改：每次替换只影响该位置之后的节点，前面记下的偏移保持有效。
        for (let i = hits.length - 1; i >= 0; i -= 1) {
            const hit = hits[i];
            const range = document.createRange();
            range.setStart(hit.node, hit.at);
            range.setEnd(hit.node, hit.at + goal.length);
            const mark = document.createElement("mark");
            mark.className = i === goalIndex ? "awb-mark awb-mark-active"
                                             : "awb-mark";
            range.surroundContents(mark);
        }
        markedElement = element;
        const active = element.querySelector(".awb-mark-active");
        const target = active || element;
        target.scrollIntoView({block: "nearest", behavior: "auto"});
        return true;
    }

    return {
        /**
         * @param {object} args {text, forward, caseSensitive, restart}
         * @param {Array<Element>} mounted 已挂载的块元素，按文档顺序
         * @return {object} {total, active}；active 是 1 基序号，0 = 没有命中
         */
        find(args, mounted)
        {
            unwrapMarks();
            const text = String(args.text || "");
            if (!text || !mounted.length) {
                activeIndex = -1;
                return {total: 0, active: 0};
            }
            const forward = args.forward !== false;
            const counts = mounted.map((element) => {
                return countIn(element.textContent, text, !!args.caseSensitive);
            });
            let total = 0;
            counts.forEach((value) => { total += value; });
            if (!total) {
                activeIndex = -1;
                return {total: 0, active: 0};
            }
            if (args.restart || activeIndex < 0 || activeIndex >= total) {
                activeIndex = forward ? 0 : total - 1;
            } else {
                activeIndex = (activeIndex + (forward ? 1 : -1) + total) % total;
            }
            // 用累计命中数定位命中所在的那一块，再在块内走文本节点
            let rest = activeIndex;
            for (let i = 0; i < mounted.length; i += 1) {
                if (counts[i] > rest) {
                    if (!markWithin(mounted[i], text, !!args.caseSensitive, rest)) {
                        break;
                    }
                    return {total: total, active: activeIndex + 1};
                }
                rest -= counts[i];
            }
            // 命中数与 DOM 不一致（并发重渲染把那一块换掉了）：报数但不跳
            return {total: total, active: 0};
        },

        // 文档被换掉时由 renderer.js 调用：旧元素上的 mark 随元素一起没了
        reset()
        {
            markedElement = null;
            activeIndex = -1;
        }
    };
}
