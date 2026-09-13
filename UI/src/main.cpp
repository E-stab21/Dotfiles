#include "barwindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("hypr-pills"));
    QApplication::setOrganizationName(QStringLiteral("dotfiles"));
    QApplication::setQuitOnLastWindowClosed(true);
    QIcon::setThemeName(QStringLiteral("Papirus-Dark"));

    BarWindow bar;
    bar.show();

    return app.exec();
}
