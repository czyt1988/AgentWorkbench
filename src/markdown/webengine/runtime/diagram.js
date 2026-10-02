// Mermaid 图表：懒注入、懒渲染、按内容缓存。
//
// 为什么图表要单独成一层：mermaid 的构建是 2.6 MB，一篇没有图表的文档不该为
// 它付这笔加载与解析时间，所以 `<script>` 是**第一个图表真的靠近视口时**才
// 插进去的。代价是这条路径上有异步与状态（queued / rendering / rendered /
// deferred / error），散在 renderer.js 里会变成没人能跟的回调网，因此收在
// 本文件，renderer.js 只在块挂载后调 install()。
//
// 页面上的可翻译文案一律不在这里：占位与失败提示的文本由调用方传进来的
// label() 取（label 读 doc.json 的 strings，见 bridge.js）。

const DEFER_LIMIT = 40;       // 图表数超过它就改成「点击再画」
const MAX_CONCURRENT = 2;     // 同时渲染的图表数（mermaid 是同步布局大户）
const SCRIPT_URL = "/vendor/mermaid/mermaid.min.js";

export function createDiagramHost(options)
{
    const label = options.label;
    const onError = options.onError || function () {};
    const variant = options.variant || function () { return "dark"; };

    const cache = new Map();          // 图表源码 → SVG 文本
    const sources = new Map();        // 图表元素 → 源码（画完后 DOM 里就没有 pre 了）
    const seen = new Set();           // 本文档里出现过的图表元素
    const queue = [];                 // 等待渲染的元素
    const watched = [];               // 已交给 IntersectionObserver 的元素
    let observer = null;
    let loader = null;                // 注入脚本的 Promise，只建一次
    let inFlight = 0;
    let idSeq = 0;
    let busy = false;                 // 本轮是否真的画过图（决定 onIdle 的边沿）
    let pumping = false;              // pump() 的重入位，见 pump() 的注释

    function sourceElement(code)
    {
        const pre = document.createElement("pre");
        pre.className = "awb-diagram-source";
        pre.textContent = code;
        return pre;
    }

    function sourceOf(element)
    {
        const pre = element.querySelector(".awb-diagram-source");
        if (pre) {
            return (pre.textContent || "").replace(/\s+$/, "");
        }
        // 画成功后源码 pre 被 SVG 整个取代，此时 DOM 里能读到的「文本」是
        // SVG 自带的那段 <style>——喂回 mermaid 就是 "No diagram type detected"
        // 的由来，而换主题重画（retheme）恰好只会发生在这个时刻。所以画完的
        // 元素一律回退到 renderOne 记下过的那份，不猜 DOM。
        return sources.get(element) || "";
    }

    function take(element)
    {
        const at = watched.indexOf(element);
        if (at >= 0) {
            watched.splice(at, 1);
        }
        if (observer) {
            observer.unobserve(element);
        }
    }

    function fail(element, message)
    {
        // 先取源码再清空：清空之后 element 里什么都不剩，那时再读只会得到
        // 空串，失败原因就永久丢了。
        const code = sourceOf(element);
        element.dataset.status = "error";
        element.replaceChildren();
        const note = document.createElement("p");
        note.className = "awb-diagram-error";
        note.textContent = label("diagramError");
        element.appendChild(note);
        element.appendChild(sourceElement(code));
        onError({kind: "diagram",
                 message: String(message || "mermaid could not render")});
        // 失败也算「这张图画完了」：不进这一趟，注入失败时队里剩下的图
        // 一张都不会被处理，idle 也永远不来——导出会一直等到超时。
        pump();
    }

    function paint(element, svg)
    {
        element.dataset.status = "rendered";
        element.innerHTML = svg;
    }

    function pump()
    {
        // 重入保护：失败与完成的路径都会回头调 pump()，让最外面那一趟把队列
        // 吃到底，而不是几十层调用栈套在 renderOne 里。被丢掉的这一趟不需要
        // 补——每个 start() 都挂了 load().then(pump)，微任务里一定会再来一次。
        if (pumping) {
            return;
        }
        pumping = true;
        while (inFlight < MAX_CONCURRENT && queue.length) {
            const element = queue.shift();
            take(element);
            if (element.dataset.status !== "queued") {
                continue;   // 同一元素被排了两次（视口进出），以后者为准
            }
            renderOne(element);
        }
        pumping = false;
        // 只在「忙过一轮且清空」时报一次 idle：那是导出前真正要等的时刻。
        // 没排过队时不报，否则浏览途中的每个滚动都会多出一条 frame 日志。
        if (busy && !queue.length && !inFlight && !watched.length) {
            busy = false;
            if (options.onIdle) {
                options.onIdle();
            }
        }
    }

    function start(element)
    {
        // 排队前一律写成 queued：pump() 只认这个状态，而 deferred 的元素是被
        // 用户点击或 renderAll() 拉起来的，状态不改就会被自己的队列弹回来。
        element.dataset.status = "queued";
        queue.push(element);
        busy = true;
        load().then(pump, (error) => fail(element, error && error.message));
    }

    function renderOne(element)
    {
        const code = sourceOf(element);
        if (!code) {
            // 空源码喂给 mermaid 只会得到一句 "No diagram type detected"，
            // 而真正的原因是这张图没有源码可读（结构被动过）。直说。
            fail(element, "diagram source is missing");
            return;
        }
        // 记下这份源码：SVG 一旦画上，元素里就没有 pre 可读，而换主题重画要的
        // 正是同一个源码（见 sourceOf）。
        sources.set(element, code);
        const cached = cache.get(code);
        if (cached) {
            paint(element, cached);
            pump();
            return;
        }
        if (!globalThis.mermaid) {
            // pump() 会经由失败路径再走进来（脚本没供出来时 element 仍在队里），
            // 这里没有 mermaid 就当场判定失败，不留一个 TypeError 给全局兜底。
            fail(element, "mermaid is not loaded");
            return;
        }
        inFlight += 1;
        element.dataset.status = "rendering";
        // mermaid 11 的 render 返回 Promise<{svg}>；startOnLoad 一律关掉，
        // 渲染时机由这里唯一决定——那是整条懒渲染管线的前提。
        globalThis.mermaid.render("awbmd" + (++idSeq), code).then((result) => {
            inFlight -= 1;
            cache.set(code, result.svg);
            paint(element, result.svg);
            pump();
        }).catch((error) => {
            inFlight -= 1;
            fail(element, error && error.message);
            pump();
        });
    }

    function ensureObserver()
    {
        if (observer) {
            return observer;
        }
        // rootMargin 400px：图表在滚到之前就画好，用户看不到「先空一段、
        // 突然长高」。代价是提前量越大屏外布局越多，400px 是两者的折中。
        observer = new IntersectionObserver((entries) => {
            entries.forEach((entry) => {
                if (!entry.isIntersecting) {
                    return;
                }
                const element = entry.target;
                take(element);
                start(element);
            });
        }, {rootMargin: "400px"});
        return observer;
    }

    function load()
    {
        if (loader) {
            return loader;
        }
        loader = new Promise((resolve, reject) => {
            const script = document.createElement("script");
            script.src = SCRIPT_URL;
            script.onload = function () {
                const mermaid = globalThis.mermaid;
                if (!mermaid) {
                    reject(new Error("mermaid global is missing"));
                    return;
                }
                mermaid.initialize({
                    startOnLoad: false,
                    // strict：图表文本里不许出现 HTML 与外链脚本。预览的是
                    // 别人写的 md 文件，这一条不是可选项。
                    securityLevel: "strict",
                    theme: variant() === "light" ? "default" : "dark"
                });
                resolve(mermaid);
            };
            script.onerror = function () {
                loader = null;    // 允许下一个图表再试一次注入
                reject(new Error("mermaid script could not be loaded"));
            };
            document.head.appendChild(script);
        });
        return loader;
    }

    function schedule(element, deferred)
    {
        element.dataset.status = deferred ? "deferred" : "queued";
        if (!deferred) {
            ensureObserver().observe(element);
            watched.push(element);
            return;
        }
        // 图表太多时不再自动排队：一篇几百张图的文档会把主线程占死几秒，
        // 而这期间用户其实只想读文字。点开才画。
        const hint = document.createElement("p");
        hint.className = "awb-diagram-hint";
        hint.textContent = label("diagramDeferred");
        element.insertBefore(hint, element.firstChild);
        element.addEventListener("click", function () {
            if (element.dataset.status === "deferred") {
                start(element);
            }
        });
    }

    return {
        /**
         * 块挂载后调用：把这一块里的图表占位交给懒渲染队列。
         */
        install(section)
        {
            section.querySelectorAll(".awb-diagram").forEach((box) => {
                if (box.dataset.status !== "queued" || seen.has(box)) {
                    return;
                }
                seen.add(box);
                schedule(box, seen.size > DEFER_LIMIT);
            });
        },

        /**
         * 导出 / 外部打开前把全部图表补上：content-visibility 与观察器会让
         * 屏外的图停在占位状态，直接序列化得到的是一排「点击渲染」。
         */
        renderAll()
        {
            const boxes = document.querySelectorAll(
                '.awb-diagram[data-status="queued"], '
                + '.awb-diagram[data-status="deferred"]');
            let first = null;
            boxes.forEach((box) => {
                take(box);
                if (!first) {
                    first = box;
                }
                start(box);
            });
            if (!first) {
                return;
            }
            load().catch((error) => {
                boxes.forEach((box) => fail(box, error && error.message));
            });
        },

        // 还没画完的图表数：renderer.js 用它决定 frame 信标里的 pendingDiagrams
        pending()
        {
            return queue.length + watched.length + inFlight;
        },

        /**
         * 主题变体变了：缓存里的 SVG 是按旧主题画的，全部作废重画。
         * mermaid 还没被加载过时什么都不用做——它的 initialize() 在第一次
         * 渲染前才跑，那时取到的就是新变体。
         */
        retheme()
        {
            if (!loader) {
                return;
            }
            cache.clear();
            const boxes = document.querySelectorAll(
                '.awb-diagram[data-status="rendered"]');
            boxes.forEach((box) => {
                const code = sourceOf(box);
                if (!code) {
                    // 没有记下源码的旧元素（换文档后 DOM 里残留的）不重画，
                    // 也不清空——宁可留一张旧主题的图，也不给一张空白。
                    return;
                }
                box.replaceChildren();
                box.appendChild(sourceElement(code));
                schedule(box, false);
            });
        },

        /**
         * 换文档：元素集合与缓存一起放下。缓存留下会跨文档串图（同一份源码
         * 在两个文档里画同一张图虽然正确，但那份内存没人再引用了）。
         */
        reset()
        {
            watched.length = 0;
            queue.length = 0;
            seen.clear();
            cache.clear();
            sources.clear();
            busy = false;
            if (observer) {
                observer.disconnect();
            }
        }
    };
}
