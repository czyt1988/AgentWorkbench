#ifndef AWB_THEME_THEMEFILE_H
#define AWB_THEME_THEMEFILE_H

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>

namespace awb::theme {

/// 一份解析完成的主题 JSON。
///
/// 令牌名是对 QML 页面的契约：每个主题都提供同一组名字，值因主题而异。
/// 无效性由 id 表达（isValid()）：ThemeLoader 判定跳过的文件以无效
/// ThemeFile 传递，调用方据此回退或忽略。
struct ThemeFile
{
    QString id;      ///< 主题 id，必须与文件名（不含 .json）一致；空串 = 无效文件
    QString name;    ///< 显示名（主题选择界面用）
    QString variant; ///< 深浅变体，只有 "dark" 与 "light" 两个合法值

    QHash<QString, QColor> colors;  ///< 颜色令牌（键 = 令牌名）
    QHash<QString, double> metrics; ///< 数值令牌：圆角、间距、字号、尺寸、时长
    QHash<QString, QString> fonts;  ///< 字体令牌，键只有 family 与 monoFamily
    QStringList agentPalette;       ///< agent 卡片按位轮换分配的 #rrggbb 颜色串

    /**
     * @brief 判断这份主题文件是否可用
     *
     * @return id 非空返回 true；解析失败或被 ThemeLoader 判定跳过时为 false
     */
    bool isValid() const { return !id.isEmpty(); }
};

} // namespace awb::theme

#endif // AWB_THEME_THEMEFILE_H
