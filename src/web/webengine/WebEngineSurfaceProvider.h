#ifndef AWB_WEB_WEBENGINESURFACEPROVIDER_H
#define AWB_WEB_WEBENGINESURFACEPROVIDER_H

#include <QObject>
#include <QString>

namespace awb::web {

class WebTabsFacade;

// Registers the `embedded` surface with the web domain's surface registry
// (specs/01 §4.6). Constructed by the application only when the build
// includes WebEngine (AWB_ENABLE_WEBENGINE).
class WebEngineSurfaceProvider : public QObject
{
    Q_OBJECT

public:
    WebEngineSurfaceProvider(WebTabsFacade *web, QObject *parent = nullptr);
};

} // namespace awb::web

#endif // AWB_WEB_WEBENGINESURFACEPROVIDER_H
