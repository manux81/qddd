#include "MdbProcess.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;

    const QString fakeMdb = temp.filePath(QStringLiteral("fake-mdb.sh"));
    QFile script(fakeMdb);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text))
        return 2;
    script.write("#!/bin/sh\n"
                 "printf '>\\n'\n"
                 "while IFS= read -r line; do\n"
                 "  printf 'ack:%s\\n>\\n' \"$line\"\n"
                 "  [ \"$line\" = quit ] && exit 0\n"
                 "done\n");
    script.close();
    if (!script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                               QFileDevice::ExeOwner))
        return 3;

    HardwareDebugConfiguration cfg = HardwareDebugConfiguration::defaultConfig();
    cfg.serverType = HardwareServerType::MplabMdb;
    cfg.serverExecutable = fakeMdb;
    cfg.mplabDevice = QStringLiteral("dsPIC33CK256MP506");
    cfg.mplabTool = QStringLiteral("PICKitBasic");
    cfg.programImage = QStringLiteral("firmware.elf");
    cfg.initialBreakpoint = QStringLiteral("main");
    cfg.startupTimeoutMs = 3000;

    MdbProcess process;
    QString output;
    QString error;
    bool ready = false;
    QEventLoop loop;
    QObject::connect(&process, &MdbProcess::outputReceived,
                     [&output](const QString& text) { output += text; });
    QObject::connect(&process, &MdbProcess::errorOccurred,
                     [&error, &loop](const QString& text) { error = text; loop.quit(); });
    QObject::connect(&process, &MdbProcess::ready, [&ready, &loop] {
        ready = true;
        loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);

    process.start(cfg);
    loop.exec();
    process.stop();

    if (!ready || !error.isEmpty())
        return 4;
    const QStringList expected = {
        QStringLiteral("ack:device dsPIC33CK256MP506"),
        QStringLiteral("ack:hwtool PICKitBasic"),
        QStringLiteral("ack:reset"),
        QStringLiteral("ack:program \"firmware.elf\""),
        QStringLiteral("ack:break main")
    };
    int position = -1;
    for (const QString& token : expected) {
        const int next = output.indexOf(token, position + 1);
        if (next < 0)
            return 5;
        position = next;
    }
    return 0;
}
