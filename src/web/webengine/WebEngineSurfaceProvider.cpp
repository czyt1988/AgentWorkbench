#include "web/webengine/WebEngineSurfaceProvider.h"

#include "web/WebTabsFacade.h"

namespace awb::web {

/**
 * @brief 构造表面提供者并登记嵌入表面
 *
 * 把 "embedded" kind 与 WebEngineSurface.qml 的 qrc URL 登记进
 * WebTabsFacade 的表面注册表——注册表里有没有这个 kind 就是
 * WebTabsFacade::engineAvailable() 的判据。本类此后无事可做，存在
 * 即生效，不留成员。
 *
 * @param web web 域的门面；须已构造
 * @param parent QObject 父项
 */
WebEngineSurfaceProvider::WebEngineSurfaceProvider(WebTabsFacade *web,
                                                   QObject *parent)
    : QObject(parent)
{
    web->registerSurface(QStringLiteral("embedded"),
                         QStringLiteral("qrc:/qt/qml/AgentWorkbench/"
                                        "web/WebEngineSurface.qml"));
}

} // namespace awb::web
