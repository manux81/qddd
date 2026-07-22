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
                 "  if [ \"$line\" = run ]; then\n"
                 "    printf 'Running\\nTarget Halted\\nStop at\\n address:0x1336a\\n file:/tmp/src/db.c\\n source line:262\\n'\n"
                 "  fi\n"
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

    auto writeElfHeader = [&temp](const QString& name, quint16 machine) {
        QByteArray header(20, '\0');
        header.replace(0, 4, QByteArray("\x7f" "ELF", 4));
        header[5] = 1; // little endian
        header[18] = static_cast<char>(machine & 0xff);
        header[19] = static_cast<char>((machine >> 8) & 0xff);
        const QString path = temp.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(header) != header.size())
            return QString();
        return path;
    };
    const QString armElf = writeElfHeader(QStringLiteral("arm.elf"), 40);
    const QString dspicElf = writeElfHeader(QStringLiteral("dspic.elf"), 118);
    cfg.programImage = armElf;
    if (cfg.validate().valid)
        return 6;
    cfg.programImage = dspicElf;
    if (!cfg.validate().valid)
        return 7;
    cfg.programImage = QStringLiteral("firmware.elf");

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

    QString stoppedFile;
    int stoppedLine = -1;
    QEventLoop locationLoop;
    QObject::connect(&process, &MdbProcess::sourceLocation,
                     [&stoppedFile, &stoppedLine, &locationLoop](const QString& file, int line) {
        stoppedFile = file;
        stoppedLine = line;
        locationLoop.quit();
    });
    QTimer::singleShot(3000, &locationLoop, &QEventLoop::quit);
    process.sendCommand(QStringLiteral("run"));
    locationLoop.exec();
    process.stop();
    if (stoppedFile != QStringLiteral("/tmp/src/db.c") || stoppedLine != 262)
        return 8;
    return 0;
}
