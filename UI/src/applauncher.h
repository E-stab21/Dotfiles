#pragma once

#include <QIcon>
#include <QString>
#include <QVector>

struct AppEntry {
    QString id;      // desktop file path
    QString name;
    QString comment;
    QString iconName;
    QString exec;
    QIcon icon;
    bool terminal = false;
};

class AppLauncher
{
public:
    static QVector<AppEntry> scan();
    static void launch(const AppEntry &app);
    static void launchById(const QString &desktopPath);
};
