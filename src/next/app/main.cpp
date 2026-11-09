#include "next/app/NextWindow.h"
#include "next/app/ProjectDocument.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PVForge Next"));
    QApplication::setApplicationDisplayName(QStringLiteral("PVForge Next"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("PVForge"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("PVForge Next — 基于新实体模型的编辑器"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption openOption(
        {QStringLiteral("o"), QStringLiteral("open")},
        QStringLiteral("启动时打开 <dir> 工程目录。"),
        QStringLiteral("dir"));
    parser.addOption(openOption);
    parser.process(app);

    pvf::next::NextWindow window(nullptr);
    window.show();

    if (parser.isSet(openOption)) {
        QString error;
        if (!window.openProject(parser.value(openOption), &error)) {
            QMessageBox::critical(&window, QStringLiteral("打开工程失败"),
                                  error.isEmpty()
                                      ? QStringLiteral("未知错误")
                                      : error);
        }
    }

    return app.exec();
}
