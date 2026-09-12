#include "applauncher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>

namespace {

QStringList desktopDirs()
{
    QStringList dirs;
    const QStringList data = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &d : data) {
        if (!d.isEmpty() && !dirs.contains(d))
            dirs.push_back(d);
    }
    const QByteArray xdg = qgetenv("XDG_DATA_DIRS");
    const QStringList parts = QString::fromLocal8Bit(xdg.isEmpty() ? "/usr/local/share:/usr/share"
                                                                   : xdg)
                                  .split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString &root : parts) {
        const QString apps = root + QStringLiteral("/applications");
        if (!dirs.contains(apps))
            dirs.push_back(apps);
    }
    return dirs;
}

QString unescapeExec(QString exec)
{
    exec.replace(QStringLiteral("%%"), QStringLiteral("\x1e"));
    exec.remove(QRegularExpression(QStringLiteral("%[fFuUdDnNickvm]")));
    exec.replace(QChar(0x1e), QLatin1Char('%'));
    return exec.simplified();
}

bool parseDesktop(const QString &path, AppEntry *out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    bool inDesktop = false;
    bool noDisplay = false;
    bool hidden = false;
    bool terminal = false;
    QString name;
    QString comment;
    QString icon;
    QString exec;
    QString type;

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('#')) || line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            inDesktop = (line == QLatin1String("[Desktop Entry]"));
            continue;
        }
        if (!inDesktop)
            continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq);
        const QString val = line.mid(eq + 1);

        if (key == QLatin1String("Type"))
            type = val;
        else if (key == QLatin1String("Name") && name.isEmpty())
            name = val;
        else if (key == QLatin1String("Comment") && comment.isEmpty())
            comment = val;
        else if (key == QLatin1String("GenericName") && comment.isEmpty())
            comment = val;
        else if (key == QLatin1String("Icon"))
            icon = val;
        else if (key == QLatin1String("Exec"))
            exec = val;
        else if (key == QLatin1String("NoDisplay"))
            noDisplay = (val == QLatin1String("true") || val == QLatin1String("1"));
        else if (key == QLatin1String("Hidden"))
            hidden = (val == QLatin1String("true") || val == QLatin1String("1"));
        else if (key == QLatin1String("Terminal"))
            terminal = (val == QLatin1String("true") || val == QLatin1String("1"));
    }

    if (noDisplay || hidden)
        return false;
    if (type != QLatin1String("Application"))
        return false;
    if (name.isEmpty() || exec.isEmpty())
        return false;

    out->id = path;
    out->name = name;
    out->comment = comment;
    out->iconName = icon;
    out->exec = unescapeExec(exec);
    out->terminal = terminal;
    if (!icon.isEmpty()) {
        if (QFile::exists(icon))
            out->icon = QIcon(icon);
        else
            out->icon = QIcon::fromTheme(icon);
    }
    if (out->icon.isNull())
        out->icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
    return true;
}

} // namespace

QVector<AppEntry> AppLauncher::scan()
{
    QVector<AppEntry> apps;
    QSet<QString> seenNames;

    for (const QString &dirPath : desktopDirs()) {
        QDir dir(dirPath);
        if (!dir.exists())
            continue;
        const QStringList files = dir.entryList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const QString &file : files) {
            AppEntry app;
            if (!parseDesktop(dir.filePath(file), &app))
                continue;
            const QString key = app.name.toLower();
            if (seenNames.contains(key))
                continue;
            seenNames.insert(key);
            apps.push_back(app);
        }
    }

    std::sort(apps.begin(), apps.end(), [](const AppEntry &a, const AppEntry &b) {
        return a.name.toLower() < b.name.toLower();
    });
    return apps;
}

void AppLauncher::launch(const AppEntry &app)
{
    if (app.exec.isEmpty())
        return;

    QStringList args = QProcess::splitCommand(app.exec);
    if (args.isEmpty())
        return;

    if (app.terminal) {
        // Always open Terminal=true apps in kitty.
        QProcess::startDetached(QStringLiteral("kitty"), args);
        return;
    }

    if (QProcess::startDetached(QStringLiteral("gtk-launch"),
                                {QFileInfo(app.id).completeBaseName()}))
        return;

    const QString program = args.takeFirst();
    QProcess::startDetached(program, args);
}

void AppLauncher::launchById(const QString &desktopPath)
{
    AppEntry app;
    if (!parseDesktop(desktopPath, &app))
        return;
    launch(app);
}
