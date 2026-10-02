#ifndef AWB_MARKDOWN_MARKDOWNPREVIEWPROVIDER_H
#define AWB_MARKDOWN_MARKDOWNPREVIEWPROVIDER_H

#include <QObject>

class QQuickWebEngineProfile;

namespace awb::markdown {

class MarkdownPreviewController;
class MarkdownRuntimeHandler;

/// 预览表面「存在即能力」的那个存在。
///
/// 构造时把 awbmd:// 的 handler 装到一个只服务于预览的 profile 上，并告诉
/// 控制器「页面现在真的能被供出去了」——控制器的 available 从此不再只看编译
/// 期开关。反过来，本类没被构造（WebEngine 关闭的构建、或者接线顺序错了）时
/// available 恒为假，菜单项与窗口都不会出现。
///
/// profile 为什么是本类自己建、而不是复用 src/web 的 WebEngineProfileStore：
/// 领域模块之间不许互相依赖（scripts/check-architecture.sh 规则 1），而预览
/// 页也没有「每个 agent 一个持久 profile」那套 cookie 隔离需求——它渲染本地
/// 内容，不留任何持久状态。
class MarkdownPreviewProvider : public QObject
{
    Q_OBJECT

public:
    explicit MarkdownPreviewProvider(MarkdownPreviewController *controller,
                                     QObject *parent = nullptr);

    /// 注册 awbmd:// 的语法与权限。**必须在 QtWebEngineQuick::initialize()
    /// 之前**调用：scheme 表在第一次创建上下文时锁死，晚注册被引擎忽略并告警。
    /// 漏掉这一步不是 404——handler 仍然服务文档与子资源，废掉的是 fetch()，
    /// 页面表现为「正文在、公式和图表永远出不来」，而构建照样全绿。这是
    /// app/main.cpp 的启动契约，构造函数会用 schemeByName() 当场核对一次。
    static void registerScheme();

    /// 预览视图要用的 profile（懒建，本类持有；同一进程内恒返回同一实例）。
    /// QML 侧的视图把它赋给 profile 属性即可。
    Q_INVOKABLE QQuickWebEngineProfile *previewProfile();

    /// 本类的 handler；测试用来直接喂请求
    MarkdownRuntimeHandler *handler() const { return m_handler; }

private:
    MarkdownPreviewController *m_controller;   ///< 能力的接收方（不拥有）
    MarkdownRuntimeHandler *m_handler;         ///< awbmd:// 的供体（本类持有）
    QQuickWebEngineProfile *m_profile = nullptr; ///< 预览专用 profile（本类持有）
};

} // namespace awb::markdown

#endif // AWB_MARKDOWN_MARKDOWNPREVIEWPROVIDER_H
