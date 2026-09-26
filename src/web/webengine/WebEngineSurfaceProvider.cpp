#include "web/webengine/WebEngineSurfaceProvider.h"

#include "web/WebTabsFacade.h"

namespace awb::web {

WebEngineSurfaceProvider::WebEngineSurfaceProvider(WebTabsFacade *web,
                                                   QObject *parent)
    : QObject(parent)
{
    web->registerSurface(QStringLiteral("embedded"),
                         QStringLiteral("qrc:/qt/qml/AgentWorkbench/"
                                        "web/WebEngineSurface.qml"));
}

} // namespace awb::web
