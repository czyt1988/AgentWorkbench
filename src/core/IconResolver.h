#ifndef AWB_CORE_ICONRESOLVER_H
#define AWB_CORE_ICONRESOLVER_H

#include <QString>

namespace awb::core {

/// 把配置里的图标字符串解析成可显示的图片 URL。
///
/// qrc:/、http(s)://、file:// 原样放行；存在的本地文件转成 file:/// URL；
/// 其余（含空串）回退到 fallback。fallback 由调用方给出——core 不写死应用资源路径。
class IconResolver
{
public:
    // 解析图标字符串；规则与回退见类注释
    static QString resolve(const QString &raw, const QString &fallback);
};

} // namespace awb::core

#endif // AWB_CORE_ICONRESOLVER_H
