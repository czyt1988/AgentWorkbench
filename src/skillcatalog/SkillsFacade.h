#ifndef AWB_SKILLS_SKILLSFACADE_H
#define AWB_SKILLS_SKILLSFACADE_H

#include "core/OpResult.h"
#include "skillcatalog/SkillScanner.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace awb::core {
class Settings;
} // namespace awb::core

namespace awb::skillcatalog {

class SkillModel;

/// skills 功能的 QML 门面：组装 SkillModel 与 SkillScanner，向页面暴露
/// 模型、根配置与统计，并承接复制/打开一类本地动作。
class SkillsFacade : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    // worker 线程上一次扫描进行中（驱动页面骨架屏与 Rescan 按钮转圈）
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    // 设置页用的根清单：[{id,label,path,kind,enabled}]。做成属性（而不只
    // 是 roots() 方法）是为了 addRoot/removeRoot/setRootEnabled 之后列表
    // 能自动重绑——裸方法调用从来不会触发重绑。
    Q_PROPERTY(QVariantList roots READ roots NOTIFY rootsChanged)
    // 页脚的 "%n skill(s) found" 风格统计文本
    Q_PROPERTY(QString statsText READ statsText NOTIFY statsChanged)
    // 最近一次扫描存在不可读/缺失的根时为 true
    Q_PROPERTY(bool partialFailure READ partialFailure NOTIFY statsChanged)

public:
    SkillsFacade(core::Settings *settings, QObject *parent = nullptr);

    // 暴露给 QML 的列表模型（QAbstractItemModel 视角）
    QAbstractItemModel *model() const;
    // 同一个模型的 SkillModel 视角（C++ 侧用）
    SkillModel *skillModel() const { return m_model; }

    bool scanning() const;
    QString statsText() const;
    bool partialFailure() const;

    // 启动入口（main.cpp 在装配期调用，QML 不调）：先同步恢复 JSON 缓存
    // ——有缓存页面立刻有数据可渲染——再发起一次后台真扫描，结果落地后
    // 更新界面并固化缓存。首次启动没有缓存时，模型在扫描完成前保持空，
    // 页面显示扫描提示。
    void start();

    // 在 worker 线程上重新扫描；scanStarted/scanFinished 夹住整个过程
    Q_INVOKABLE void refresh();

    // 设置页用的根清单（Q_PROPERTY 的 READ——见上方说明）
    QVariantList roots() const;
    // 切换某个根的启用状态（写进 skills.roots 并发 rootsChanged）
    Q_INVOKABLE void setRootEnabled(const QString &id, bool enabled);
    // 追加一个自定义根（路径来自设置页）；空路径返回 false
    Q_INVOKABLE bool addRoot(const QString &path);
    // 按 id 移除一个根；没找到该 id 时返回 false 且不落盘
    Q_INVOKABLE bool removeRoot(const QString &id);

    // 返回类型必须写全限定名：Qt 5 的 moc 按头文件书写形式记录返回类型名，
    // QML 调用端按 QMetaType 注册名（即类全名 awb::core::OpResult）解析；
    // 短名解析不到注册表就抛 "Unknown method return type"，调用静默失效。
    // 以下三个分别把 skill 的目录路径/文件路径/名称写进剪贴板；
    // toast 由调用方按 OpResult 展示。
    Q_INVOKABLE awb::core::OpResult copyPath(const QString &skillFilePath);
    Q_INVOKABLE awb::core::OpResult copySkillFile(const QString &skillFilePath);
    Q_INVOKABLE awb::core::OpResult copyName(const QString &skillFilePath);

    // 用系统默认方式打开 skill 所在目录
    Q_INVOKABLE awb::core::OpResult openFolder(const QString &skillFilePath);
    // 在文件管理器里定位并选中 SKILL.md 本体
    Q_INVOKABLE awb::core::OpResult revealSkillFile(const QString &skillFilePath);

    // 按 SKILL.md 路径取定义（悬停 flyout 用）；未知路径返回空 map
    Q_INVOKABLE QVariantMap skill(const QString &skillFilePath) const;

    // 根类别 id 的显示名（"agents" -> "Agents"…）。Settings 与 Skills
    // 两个页面共用这一处——之前这段字面 switch 抄在 QML 里，lupdate
    // 看不见它，翻译维护会漏。
    Q_INVOKABLE QString kindLabel(const QString &kind) const;

Q_SIGNALS:
    /**
     * @brief 扫描进行中状态翻转时发射（转发 scanner 的边沿信号）
     */
    void scanningChanged();

    /**
     * @brief 统计文本或 partialFailure 可能变化时发射
     */
    void statsChanged();

    /**
     * @brief 根清单变化（增删/开关）时发射，页面据此重绑列表
     */
    void rootsChanged();

    /**
     * @brief 真扫描开始时发射（缓存恢复不发）
     */
    void scanStarted();

    /**
     * @brief 一次扫描的结果落地时发射（模型已更新）
     */
    void scanFinished();

private:
    // 按 SKILL.md 路径在最近一次结果里查定义；找不到返回 nullptr
    const SkillDefinition *find(const QString &skillFilePath) const;
    // 取 skillFilePath 所在目录（QFileInfo 的 absolutePath）
    static QString parentDir(const QString &skillFilePath);
    // copyPath/copySkillFile/copyName 共用的收尾：写剪贴板
    core::OpResult copyToClipboard(const QString &text);

    core::Settings *m_settings;  ///< 设置访问层（写 skills.roots 用）
    SkillModel *m_model;         ///< 页面模型
    SkillScanner *m_scanner;     ///< 扫描协调者
};

} // namespace awb::skillcatalog

#endif // AWB_SKILLS_SKILLSFACADE_H
