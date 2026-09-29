#include "agentcatalog/AgentUrls.h"

#include "core/EnvExpander.h"

#include <QFile>
#include <QIODevice>
#include <QRegularExpression>
#include <QUrl>

namespace awb::agentcatalog {

namespace {

/**
 * @brief 判断 host 是否为回环地址
 *
 * 认可的写法：127.0.0.1、localhost、::1 以及任意 127.x.x.x；大小写不敏感。
 *
 * @param host URL 的 host 部分
 * @return 是回环写法时返回 true
 */
bool isLoopbackHost(const QString &host)
{
    const QString h = host.toLower();
    return h == QStringLiteral("127.0.0.1") || h == QStringLiteral("localhost")
           || h == QStringLiteral("::1") || h.startsWith(QStringLiteral("127."));
}

/**
 * @brief 判断两个 URL 是否指向同一服务器
 *
 * host 相等即视为相同（大小写不敏感；两边的回环写法互认），端口比对用
 * 有效端口——URL 未写端口时取 scheme 默认值（https 为 443，其余 80）。
 *
 * @param a 待比对 URL
 * @param b 待比对 URL
 * @return host 与有效端口都相同时返回 true；任一 host 为空返回 false
 */
bool sameServer(const QUrl &a, const QUrl &b)
{
    if (a.host().isEmpty() || b.host().isEmpty()) {
        return false;
    }
    const bool sameHost =
            a.host().compare(b.host(), Qt::CaseInsensitive) == 0
            || (isLoopbackHost(a.host()) && isLoopbackHost(b.host()));
    if (!sameHost) {
        return false;
    }
    const auto effectivePort = [](const QUrl &u) {
        const int port = u.port(-1);
        if (port != -1) {
            return port;
        }
        return u.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
                ? 443 : 80;
    };
    return effectivePort(a) == effectivePort(b);
}

} // namespace

/**
 * @brief 读出 tokenFile 里的 bearer token
 *
 * 路径先经 EnvExpander 展开（支持 %VAR% 与 ~），再以只读文本方式打开，
 * 内容按 UTF-8 解码并去掉首尾空白。
 *
 * @param tokenFile token 文件路径；空串表示未配置
 * @return token 值；未配置、文件打不开或内容空白时返回空串
 */
QString AgentUrls::tokenValue(const QString &tokenFile)
{
    if (tokenFile.isEmpty()) {
        return {};
    }
    QFile f(core::EnvExpander::expand(tokenFile));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll()).trimmed();
}

/**
 * @brief 取 agent 定义对应的最终 URL
 *
 * @param definition agent 定义，取其中的 webUrl 与 tokenFile
 * @return finalUrl(webUrl, tokenFile) 的结果
 * @sa finalUrl(const QString &, const QString &)
 */
QString AgentUrls::finalUrl(const AgentDefinition &definition)
{
    return finalUrl(definition.webUrl, definition.tokenFile);
}

/**
 * @brief 把 token 合并进 base URL
 *
 * token 以 URL 片段（#token=<value>）追加，而不是查询参数：片段从不发给
 * 服务器，token 因此不进访问日志与 Referer 头。base 已带片段时用 & 连接——
 * 再写一个 # 会让其后的内容全部落进第一个片段的文本里。已自带鉴权信息的
 * base（dsh 的 ?token=… 会话 URL）不再叠第二个 token。web UI 靠该片段向
 * 变更类路由（如 POST /workspaces）鉴权。
 *
 * @param baseUrl   原始地址；空串原样返回
 * @param tokenFile token 文件路径；token 为空时 base 原样返回
 * @return 追加了 token 片段的 URL；不适用时返回 baseUrl 本身
 */
QString AgentUrls::finalUrl(const QString &baseUrl, const QString &tokenFile)
{
    if (baseUrl.isEmpty()) {
        return baseUrl;
    }

    const QString token = tokenValue(tokenFile);
    if (token.isEmpty() || baseUrl.contains(QStringLiteral("token="))) {
        return baseUrl;
    }
    return baseUrl
           + (baseUrl.contains(QLatin1Char('#')) ? QStringLiteral("&token=")
                                                 : QStringLiteral("#token="))
           + token;
}

/**
 * @brief 从 agent 的启动输出里挑出会话 URL
 *
 * 带 token 门禁的 harness（dsh 一类）把每进程随机的带 token URL 打到
 * stdout 而非写 token 文件，所以这里扫输出文本而不是读文件。只取第一条
 * 指向同一服务器（sameServer：host 互认回环写法、有效端口相等）的 URL，
 * 避免把文档链接误当会话入口。
 *
 * @param output 启动输出（已做 token 脱敏）
 * @param webUrl agent 配置的 web 地址，用于比对服务器
 * @return 找到的会话 URL；输出为空、webUrl 非法或没有匹配时返回空串
 * @sa sameServer
 */
QString AgentUrls::sessionUrlFromOutput(const QString &output,
                                        const QString &webUrl)
{
    const QUrl web(webUrl);
    if (output.isEmpty() || !web.isValid() || web.host().isEmpty()) {
        return {};
    }

    static const QRegularExpression re(
            QStringLiteral("https?://[^\\s\"'<>]+"),
            QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = re.globalMatch(output);
    while (it.hasNext()) {
        QString candidate = it.next().captured(0);
        // 控制台行会把句读紧贴在 URL 后面，逐个摘掉再解析。
        while (candidate.endsWith(QLatin1Char('.'))
               || candidate.endsWith(QLatin1Char(','))
               || candidate.endsWith(QLatin1Char(';'))
               || candidate.endsWith(QLatin1Char(')'))) {
            candidate.chop(1);
        }
        const QUrl parsed(candidate);
        if (parsed.isValid() && sameServer(parsed, web)) {
            return candidate;
        }
    }
    return {};
}

} // namespace awb::agentcatalog
