#ifndef AWB_WEB_WEBENGINESURFACEPROVIDER_H
#define AWB_WEB_WEBENGINESURFACEPROVIDER_H

#include <QObject>
#include <QString>

namespace awb::web {

class WebTabsFacade;

/// 向 web 域的表面注册表登记 `embedded` 表面。
///
/// 仅当构建包含 WebEngine（AWB_ENABLE_WEBENGINE=ON）时由应用构造，
/// 它的存在因此就是「嵌入表面可用」的标识。无成员、无信号——全部工作
/// 在构造函数里完成。
class WebEngineSurfaceProvider : public QObject
{
    Q_OBJECT

public:
    // 构造即把 "embedded" -> WebEngineSurface.qml 登记进 web 的注册表
    WebEngineSurfaceProvider(WebTabsFacade *web, QObject *parent = nullptr);
};

} // namespace awb::web

#endif // AWB_WEB_WEBENGINESURFACEPROVIDER_H
