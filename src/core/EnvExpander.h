#ifndef AWB_CORE_ENVEXPANDER_H
#define AWB_CORE_ENVEXPANDER_H

#include <QString>

namespace awb::core {

/// 各模块共用的路径展开：%VAR%（Windows 风格）环境变量与前导 ~（home 目录）。
class EnvExpander
{
public:
    // 展开 %VAR% 与 "~/"；未知变量原样保留
    static QString expand(const QString &path);
};

} // namespace awb::core

#endif // AWB_CORE_ENVEXPANDER_H
