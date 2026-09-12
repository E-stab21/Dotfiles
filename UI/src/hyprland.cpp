#include "hyprland.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QTimer>

#include <algorithm>

HyprlandClient::HyprlandClient(QObject *parent)
    : QObject(parent)
{
    refresh();
    connectEventSocket();

    auto *timer = new QTimer(this);
    timer->setInterval(2000);
    connect(timer, &QTimer::timeout, this, &HyprlandClient::refresh);
    timer->start();
}

QString HyprlandClient::socketDir() const
{
    const QByteArray sig = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (sig.isEmpty())
        return {};

    const QString runtime = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR"));
    if (runtime.isEmpty())
        return {};

    const QString modern = runtime + QStringLiteral("/hypr/") + QString::fromUtf8(sig);
    if (QDir(modern).exists())
        return modern;

    return QStringLiteral("/tmp/hypr/") + QString::fromUtf8(sig);
}

QByteArray HyprlandClient::hyprctl(const QStringList &args) const
{
    QProcess proc;
    proc.start(QStringLiteral("hyprctl"), args);
    if (!proc.waitForFinished(1500))
        return {};
    return proc.readAllStandardOutput();
}

void HyprlandClient::refresh()
{
    const QByteArray raw = hyprctl({QStringLiteral("-j"), QStringLiteral("workspaces")});
    const QByteArray activeRaw = hyprctl({QStringLiteral("-j"), QStringLiteral("activeworkspace")});

    const auto activeDoc = QJsonDocument::fromJson(activeRaw);
    m_activeId = activeDoc.object().value(QStringLiteral("id")).toInt(m_activeId);

    QVector<WorkspaceInfo> next;
    const auto doc = QJsonDocument::fromJson(raw);
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        WorkspaceInfo ws;
        ws.id = o.value(QStringLiteral("id")).toInt();
        ws.name = o.value(QStringLiteral("name")).toString();
        ws.active = (ws.id == m_activeId);
        if (ws.id > 0)
            next.push_back(ws);
    }

    std::sort(next.begin(), next.end(), [](const WorkspaceInfo &a, const WorkspaceInfo &b) {
        return a.id < b.id;
    });

    if (next.isEmpty()) {
        for (int i = 1; i <= 5; ++i)
            next.push_back({i, QString::number(i), i == m_activeId});
    }

    if (next == m_workspaces)
        return;

    m_workspaces = next;
    emit changed();
}

void HyprlandClient::focusWorkspace(int id)
{
    hyprctl({QStringLiteral("dispatch"), QStringLiteral("workspace"), QString::number(id)});
    refresh();
}

void HyprlandClient::openLauncher()
{
    QProcess::startDetached(QStringLiteral("rofi"), {QStringLiteral("-show"), QStringLiteral("drun")});
}

void HyprlandClient::connectEventSocket()
{
    const QString dir = socketDir();
    if (dir.isEmpty())
        return;

    const QString path = dir + QStringLiteral("/.socket2.sock");
    if (!QFile::exists(path))
        return;

    m_events = new QLocalSocket(this);
    connect(m_events, &QLocalSocket::readyRead, this, [this]() {
        m_eventBuf += m_events->readAll();
        while (true) {
            const int nl = m_eventBuf.indexOf('\n');
            if (nl < 0)
                break;
            const QByteArray line = m_eventBuf.left(nl);
            m_eventBuf.remove(0, nl + 1);
            handleEventLine(line);
        }
    });
    connect(m_events, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        QTimer::singleShot(1500, this, &HyprlandClient::connectEventSocket);
    });
    connect(m_events, &QLocalSocket::disconnected, this, [this]() {
        QTimer::singleShot(1500, this, &HyprlandClient::connectEventSocket);
    });
    m_events->connectToServer(path);
}

void HyprlandClient::handleEventLine(const QByteArray &line)
{
    if (line.startsWith("workspace>>") || line.startsWith("focusedmon>>")
        || line.startsWith("createworkspace>>") || line.startsWith("destroyworkspace>>")
        || line.startsWith("activewindow>>")) {
        refresh();
    }
}
