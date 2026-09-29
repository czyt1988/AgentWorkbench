#ifndef AWB_AGENTS_AGENTURLS_H
#define AWB_AGENTS_AGENTURLS_H

#include "agentcatalog/AgentDefinition.h"

#include <QString>

namespace awb::agentcatalog {

/// agent 的 URL 与 token 处理。
///
/// 内嵌视图与外部浏览器共用它，保证两边打开的最终 URL 完全一致。
class AgentUrls
{
public:
    // 最终打开的 URL：必要时把 token 追加为 #token= 片段，不落服务器日志
    static QString finalUrl(const AgentDefinition &definition);

    // 同样的合并，作用于任意 base（如启动输出里抓到的会话 URL）；已带 token= 的原样返回
    static QString finalUrl(const QString &baseUrl, const QString &tokenFile);

    // 从 tokenFile（路径经环境变量展开）读出的 bearer token；未配置、缺失或空白返回空串
    static QString tokenValue(const QString &tokenFile);

    // 从 agent 的启动输出里挑出指向 webUrl 同一服务器的第一条 URL；
    // 无匹配返回空串
    static QString sessionUrlFromOutput(const QString &output,
                                        const QString &webUrl);
};

} // namespace awb::agentcatalog

#endif // AWB_AGENTS_AGENTURLS_H
