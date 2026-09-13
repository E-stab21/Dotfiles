#include "barwindow.h"

#include <QApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

#include <unistd.h>

namespace {

QString ipcName()
{
    return QStringLiteral("hypr-pills-%1").arg(getuid());
}

bool sendIpc(const QByteArray &command)
{
    QLocalSocket sock;
    sock.connectToServer(ipcName());
    if (!sock.waitForConnected(300))
        return false;
    sock.write(command);
    sock.write("\n");
    sock.flush();
    sock.waitForBytesWritten(300);
    sock.disconnectFromServer();
    return true;
}

void handleClient(QLocalSocket *client, BarWindow *bar)
{
    auto consume = [client, bar]() {
        while (client->bytesAvailable() > 0) {
            QByteArray chunk = client->canReadLine() ? client->readLine() : client->readAll();
            for (const QByteArray &line : chunk.split('\n')) {
                if (line.trimmed() == QByteArrayLiteral("launcher"))
                    bar->toggleLauncher();
            }
        }
    };
    QObject::connect(client, &QLocalSocket::readyRead, bar, consume);
    QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
    consume();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("hypr-pills"));
    QApplication::setOrganizationName(QStringLiteral("dotfiles"));
    QApplication::setQuitOnLastWindowClosed(true);
    QIcon::setThemeName(QStringLiteral("Papirus-Dark"));

    const bool wantLauncher = app.arguments().contains(QStringLiteral("--launcher"));

    // Hand off to the already-running bar when possible.
    if (wantLauncher && sendIpc(QByteArrayLiteral("launcher")))
        return 0;

    QLocalServer::removeServer(ipcName());
    QLocalServer server;
    if (!server.listen(ipcName())) {
        // Another instance won the race — retry handoff, then give up.
        if (wantLauncher && sendIpc(QByteArrayLiteral("launcher")))
            return 0;
        return 1;
    }

    BarWindow bar;
    QObject::connect(&server, &QLocalServer::newConnection, &bar, [&server, &bar]() {
        while (QLocalSocket *client = server.nextPendingConnection())
            handleClient(client, &bar);
    });

    bar.show();
    if (wantLauncher)
        QTimer::singleShot(50, &bar, &BarWindow::toggleLauncher);

    return app.exec();
}
