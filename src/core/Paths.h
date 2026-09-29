#ifndef AWB_CORE_PATHS_H
#define AWB_CORE_PATHS_H

#include <QString>

namespace awb::core {

/// 应用数据目录的唯一来源，其它模块一律经它取路径，不得自行推导数据根。
class Paths
{
public:
    // <dataRoot>，默认 ~/.AgentWorkbench；优先级见 Paths.cpp
    static QString dataRoot();

    // 把 dataRoot() 指向固定目录（测试注入 QTemporaryDir 用），空串恢复默认
    static void setDataRootForTesting(const QString &dir);

    // <dataRoot>/themes
    static QString themesDir();
    // <dataRoot>/plugins
    static QString pluginsDir();
    // <dataRoot>/log
    static QString logsDir();
    // <dataRoot>/webprofiles
    static QString webProfilesDir();
    // <dataRoot>/skills_cache.json —— skillcatalog 的扫描结果缓存。
    static QString skillCacheFile();

    // ~/Downloads（将来允许用户改）
    static QString downloadsDir();

    // dataRoot() 是否已被 setDataRootForTesting() 覆盖
    static bool isDataRootOverridden();

private:
    static QString s_testRoot;  ///< 测试注入的数据根，空串表示未覆盖
};

} // namespace awb::core

#endif // AWB_CORE_PATHS_H
