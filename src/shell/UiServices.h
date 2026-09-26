#ifndef AWB_SHELL_UISERVICES_H
#define AWB_SHELL_UISERVICES_H

#include "core/OpResult.h"

#include <QObject>
#include <QUrl>

namespace awb::shell {

// Generic UI operations the pages need but must not implement themselves
// (01-architecture.md §4.7): clipboard, external URLs, file manager.
// QML only talks to facades — never to Qt directly.
class UiServices : public QObject
{
    Q_OBJECT

public:
    explicit UiServices(QObject *parent = nullptr);

    // Copy to the system clipboard. Failure (e.g. no clipboard service)
    // comes back as OpResult so the caller can show the reason.
    Q_INVOKABLE core::OpResult copyText(const QString &text);

    // Open a URL/path with the system handler (browser, file association).
    Q_INVOKABLE core::OpResult openExternalUrl(const QUrl &url);

    // Reveal a file in the file manager (explorer /select on Windows).
    Q_INVOKABLE core::OpResult revealFile(const QString &path);

    // Open a folder in the file manager.
    Q_INVOKABLE core::OpResult openFolder(const QString &path);
};

} // namespace awb::shell

#endif // AWB_SHELL_UISERVICES_H
