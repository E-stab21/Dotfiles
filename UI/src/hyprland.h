#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>

struct WorkspaceInfo {
    int id = 0;
    QString name;
    bool active = false;

    bool operator==(const WorkspaceInfo &other) const
    {
        return id == other.id && name == other.name && active == other.active;
    }
};

class HyprlandClient : public QObject
{
    Q_OBJECT
public:
    explicit HyprlandClient(QObject *parent = nullptr);

    QVector<WorkspaceInfo> workspaces() const { return m_workspaces; }
    int activeId() const { return m_activeId; }

public slots:
    void refresh();
    void focusWorkspace(int id);
    void openLauncher();

signals:
    void changed();

private:
    void connectEventSocket();
    void handleEventLine(const QByteArray &line);
    QByteArray hyprctl(const QStringList &args) const;
    QString socketDir() const;

    QVector<WorkspaceInfo> m_workspaces;
    int m_activeId = 1;
    class QLocalSocket *m_events = nullptr;
    QByteArray m_eventBuf;
};
