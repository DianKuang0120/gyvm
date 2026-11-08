#include "mainwindow.h"
#include "style.h"

#include <QApplication>
#include <QIcon>
#include <QStyleFactory>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("gyvm"));
    QApplication::setOrganizationName(QStringLiteral("gyvm"));
    QApplication::setApplicationDisplayName(QStringLiteral("gyvm"));

    // Fusion 作为可预测的基底，再套我们的深色 QSS；图标用 breeze-dark。
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QIcon::setThemeName(QStringLiteral("breeze-dark"));
    app.setStyleSheet(QString::fromUtf8(kStyleSheet));

    MainWindow w;
    w.show();
    return app.exec();
}
