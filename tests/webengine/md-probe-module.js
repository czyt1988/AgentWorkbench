// tst_webengine 专用的探针页脚本：它不是产品运行时的一部分（产品那份在
// src/markdown/runtime/，commit 5 落地），只验证 awbmd:// 这条供体链在真实
// Chromium 上成立——scheme 注册过、handler 能读 Qt 资源、MIME 正确到
// 「模块脚本真的执行」、同源 fetch 拿得到 doc.json、KaTeX 的 woff2 供出并
// 解码、文档相对图片读得到、GET 信标回 200。
//
// 结果只有一个出口：document.title。测试侧没有 C++ 的 runJavaScript 入口
// （QQuickWebEngineView 不把它暴露给 C++），标题回流是本仓库已经验证过的
// 通道（见 testCompatPolyfillsAreInjected）。
const missing = [];

function need(ok, name)
{
    if (!ok) {
        missing.push(name);
    }
}

// 文档相对图片：/asset/<docId>/<rel>，docId 从 doc.json 里取（页面自己不拼
// 路径形状这件事由 C++ 侧的 MarkdownRuntime 负责，这里只照它给的 URL 用）。
async function probeAssetImage(docId)
{
    const url = '/asset/' + encodeURIComponent(docId) + '/probe-image.png';
    return new Promise((resolve) => {
        const image = new Image();
        image.onload = () => resolve(true);
        image.onerror = () => resolve(false);
        image.src = url;
    });
}

async function run()
{
    // 1) 能执行到这里就说明模块脚本的 Content-Type 是 JS。
    need(typeof document !== 'undefined', 'moduleRan');

    // 2) 同源 fetch：要 scheme 带 FetchApiAllowed（6.6+）与 CorsEnabled。
    //    doc.json 由 handler 现场向控制器要，所以它同时验到文档通道。
    let doc = null;
    try {
        const resp = await fetch('/doc.json?rev=1', { cache: 'no-store' });
        need(resp.ok, 'docFetchStatus');
        doc = await resp.json();
    } catch (e) {
        missing.push('docFetch:' + e.name);
    }
    need(doc && typeof doc.rev === 'number', 'docRev');
    need(doc && typeof doc.docId === 'string' && doc.docId.length > 0, 'docId');
    need(doc && Array.isArray(doc.blocks), 'docBlocks');

    // 3) vendor 的 KaTeX：全局对象带 renderToString 才算那份 JS 供出并执行了
    //    （katex 是带方法的对象，不是函数——按 typeof === 'function' 判会一直
    //    假失败，把「渲染其实正常」误报成缺环）。
    const katexReady = typeof window.katex?.renderToString === 'function';
    need(katexReady, 'katexGlobal');
    if (katexReady) {
        try {
            document.getElementById('math').innerHTML =
                window.katex.renderToString('\\frac{\\pi}{2}');
        } catch (e) {
            missing.push('katexRender:' + e.name);
        }
        need(document.querySelector('.katex') !== null, 'katexDom');
    }

    // 4) 字体：@font-face 来自 vendor 的 CSS，src 是相对路径。load() 兑现
    //    非空意味着 woff2 的字节以字体 MIME 供出且被解码——路径映射或 MIME
    //    任一处错，这里就一直 pending。
    try {
        const faces = await document.fonts.load('16px KaTeX_Main', '\u03c0');
        need(faces.length > 0, 'katexFont');
    } catch (e) {
        missing.push('katexFont:' + e.name);
    }

    // 5) 文档目录里的相对资源（图片）。
    if (doc && doc.docId) {
        need(await probeAssetImage(doc.docId), 'assetImage');
    } else {
        missing.push('assetImage:noDocId');
    }

    // 6) GET 信标：C++ 侧由 handler 派给 controller->reportError()，
    //    测试用信号间谍确认它真的落到了；这里只确认请求回了 200。
    try {
        const resp = await fetch('/event?k=error&p='
                                 + encodeURIComponent('{"m":"probe"}'),
                                 { cache: 'no-store' });
        need(resp.ok, 'beaconStatus');
    } catch (e) {
        missing.push('beacon:' + e.name);
    }

    // 7) 未知路径必须是错误，而不是把别的东西供出去。
    try {
        const resp = await fetch('/no-such-file.xyz', { cache: 'no-store' });
        need(!resp.ok, 'unknownPathAccepted');
    } catch (e) {
        // fetch 直接 reject 也算「没有供出内容」，符合预期。
    }

    document.title = missing.length === 0
        ? 'AWBMD-OK' : 'AWBMD-FAIL:' + missing.join(',');
}

run().catch((e) => {
    document.title = 'AWBMD-THROW:' + e;
});
