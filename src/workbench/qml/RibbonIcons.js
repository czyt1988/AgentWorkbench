// 命令条图标的唯一来源：24×24 描边字形（2px、圆角端点）按需染色成 data
// URL SVG。
//
// 为什么不用静态 SVG 文件：仓库里的 icons/*.svg 颜色写死（灰蓝单色），
// SARibbon 的按钮叶子对 Image 不做任何着色，深浅主题下要么发灰要么发
// 糊。这里把 path 数据当代码管理，iconUrl() 在使用点用主题令牌现染——
// 亮暗两套主题同一份字形，颜色永远跟 theme.textPrimary 走。QML Image
// 对 data URL 的 SVG 两代引擎都能渲染（Qt 5.15 实测栅格正确）。
//
// 字形约定：viewBox 24×24、fill none、stroke-width 2、round cap/join；
// 只画「功能语义」必需的笔画，不复用不相关图标。

.pragma library

/// 图标名 → SVG 内部标记（不含 <svg> 外壳）。
var PATHS = {
    // --- 剪贴板 ---
    "paste": '<rect x="9" y="3" width="6" height="3" rx="1"/>'
           + '<path d="M16 4h2a2 2 0 0 1 2 2v13a2 2 0 0 1-2 2H6'
           + 'a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2"/>'
           + '<path d="M9 11h6"/><path d="M9 15h4"/>',
    "cut": '<circle cx="6" cy="6" r="3"/><circle cx="6" cy="18" r="3"/>'
         + '<path d="M20 4 8.1 15.9"/><path d="m14.5 14.5 5.5 5.5"/>'
         + '<path d="M8.1 8.1 12 12"/>',
    "undo": '<path d="M3 7v6h6"/>'
          + '<path d="M21 17a9 9 0 0 0-15-6.7L3 13"/>',
    "redo": '<path d="M21 7v6h-6"/>'
          + '<path d="M3 17a9 9 0 0 1 15-6.7L21 13"/>',

    // --- 行内格式 ---
    "bold": '<path d="M7 4h6.5a4 4 0 0 1 0 8H7z"/>'
          + '<path d="M7 12h7.5a4 4 0 0 1 0 8H7z"/>',
    "italic": '<path d="M19 4h-9"/><path d="M14 20H5"/>'
            + '<path d="M15 4 9 20"/>',
    "underline": '<path d="M7 4v7a5 5 0 0 0 10 0V4"/>'
               + '<path d="M5 20h14"/>',
    "strike": '<path d="M16 5c-.7-.7-2.1-1.2-3.5-1.2-2.2 0-3.8 1.2-3.8 2.8'
            + ' 0 1.5 1.2 2.4 3.3 2.9"/>'
            + '<path d="M15.5 13.5c.9.7 1.5 1.6 1.5 2.7 0 1.9-2 3.3-4.5 3.3'
            + '-1.7 0-3.2-.6-4-1.5"/>'
            + '<path d="M4 12h16"/>',
    "link": '<path d="M10 13a5 5 0 0 0 7.5.5l3-3a5 5 0 0 0-7-7l-1.7 1.7"/>'
          + '<path d="M14 11a5 5 0 0 0-7.5-.5l-3 3a5 5 0 0 0 7 7l1.7-1.7"/>',

    // --- 列表与块 ---
    "bullet": '<path d="M8 6h13"/><path d="M8 12h13"/><path d="M8 18h13"/>'
            + '<path d="M3.5 6h.01"/><path d="M3.5 12h.01"/>'
            + '<path d="M3.5 18h.01"/>',
    "numbered": '<path d="M9 6h12"/><path d="M9 12h12"/><path d="M9 18h12"/>'
              + '<path d="M3.2 5.2 4.4 4.4v5.6"/>'
              + '<path d="M3 14.4c0-1.1.8-1.9 1.7-1.9.9 0 1.6.7 1.6 1.6'
              + ' 0 1.9-3.3 2.1-3.3 4.6h3.6"/>',
    "task": '<path d="m2.5 5.5 1.5 1.5 3-3"/><path d="M10 6h11"/>'
          + '<path d="m2.5 11.5 1.5 1.5 3-3"/><path d="M10 12h11"/>'
          + '<path d="m2.5 17.5 1.5 1.5 3-3"/><path d="M10 18h11"/>',
    "quote": '<path d="M4 5v14"/><path d="M8 8h12"/><path d="M8 12h12"/>'
           + '<path d="M8 16h8"/>',
    "divider": '<path d="M4 5h16"/><path d="M3 12h18"/><path d="M4 19h16"/>',
    "heading": '<path d="M6 4v16"/><path d="M18 4v16"/><path d="M6 12h12"/>',

    // --- 插入 ---
    "inlineCode": '<path d="m10 8-2.5 4L10 16"/>'
                + '<path d="m14 8 2.5 4L14 16"/>',
    "codeBlock": '<rect x="3" y="4" width="18" height="16" rx="2"/>'
               + '<path d="m9.5 10-2 2 2 2"/><path d="m14.5 10 2 2-2 2"/>',
    "table": '<rect x="3" y="4" width="18" height="16" rx="2"/>'
           + '<path d="M3 10h18"/><path d="M10 10v10"/>',

    // --- 交付与提示词域 ---
    "copy": '<rect x="9" y="9" width="12" height="12" rx="2"/>'
          + '<path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9'
          + 'a2 2 0 0 1 2 2v1"/>',
    "template": '<rect x="3" y="4" width="18" height="16" rx="2"/>'
              + '<path d="M3 9h18"/><path d="M9 9v11"/>',
    "favorite": '<path d="m12 3 2.7 5.6 6.1.9-4.4 4.3 1 6.1-5.4-2.9'
              + '-5.4 2.9 1-6.1L3.2 9.5l6.1-.9z"/>',
    "check": '<path d="M12 3 5 5.5V11c0 4.6 3 7.9 7 10 4-2.1 7-5.4 7-10V5.5z"/>'
           + '<path d="m9 11.5 2 2 4-4"/>',
    "history": '<path d="M3.5 12a8.5 8.5 0 1 0 2.6-6.1L3.5 8.4"/>'
             + '<path d="M3.5 3.5v5h5"/><path d="M12 7.5V12l3 3"/>',

    // --- 视图 ---
    "preview": '<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5'
             + ' 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="3"/>',
    "split": '<rect x="3" y="4" width="18" height="16" rx="2"/>'
           + '<path d="M12 4v16"/>',
    "source": '<path d="m8 6-5.5 6L8 18"/><path d="m16 6 5.5 6L16 18"/>',
    "fontSize": '<path d="M2.5 19 7 6.5 11.5 19"/><path d="M4 15h6"/>'
              + '<path d="m14.5 19 3-8.5 3 8.5"/><path d="M15.4 16.5h4.2"/>',
    "width": '<path d="M3 12h18"/><path d="m7 8-4 4 4 4"/>'
           + '<path d="m17 8 4 4-4 4"/>',
    "outline": '<path d="M5 4v16"/><path d="M9 8h11"/>'
             + '<path d="M9 13h11"/><path d="M9 18h7"/>',

    // --- 配套小件 ---
    "search": '<circle cx="11" cy="11" r="7"/>'
            + '<path d="m20.5 20.5-4.2-4.2"/>',
    "chevronLeft": '<path d="m14 6-6 6 6 6"/>',
    "chevronRight": '<path d="m10 6 6 6-6 6"/>',
    "close": '<path d="M6 6l12 12"/><path d="M18 6 6 18"/>',
    "plus": '<path d="M12 5v14"/><path d="M5 12h14"/>'
};

/**
 * 图标名 + 颜色 → 可直接赋给 Image.source / iconSource 的 data URL。
 *
 * @param name PATHS 里的图标名；未知名返回空串（Image 显示空，不抛）
 * @param color 十六进制颜色串（主题令牌的 QColor 在 QML 拼接时自动
 *              toString 成 "#rrggbb"）
 * @return data URL；未知名返回 ""
 */
function iconUrl(name, color) {
    const body = PATHS[name];
    if (body === undefined) {
        return "";
    }
    const svg = '<svg xmlns="http://www.w3.org/2000/svg" width="24" '
            + 'height="24" viewBox="0 0 24 24" fill="none" stroke="'
            + color + '" stroke-width="2" stroke-linecap="round" '
            + 'stroke-linejoin="round">' + body + "</svg>";
    return "data:image/svg+xml;utf8," + encodeURIComponent(svg);
}
