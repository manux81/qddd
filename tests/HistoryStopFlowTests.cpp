// End-to-end stop-history flow over a scripted GDB/MI session (no UI).
//
// Drives the REAL GdbMiSession through three stops with changing values and
// wires History the way the UI does (backend -> HistorySession -> resolver
// -> DisplayedStateModel). Proves with production signal ordering that the
// event<->snapshot link is 1:1 by stepIndex, that selecting an old stop
// displays that stop's values, that Go Live restores the latest, and that
// browsing history never commands the target (the script logs every MI
// command line; no -exec-* traffic may appear).
#include "GdbMiSession.h"
#include "DebugSession.h"
#include "history/DisplayedState.h"
#include "history/GdbStopHistoryBackend.h"
#include "history/HistorySession.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDevice>
#include <QTemporaryDir>
#include <QThread>

#include <functional>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; return 1; } } while(false)

using namespace qddd::history;

namespace {

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 10000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

QString createScriptedGdb(QTemporaryDir &temp, const QString &logPath)
{
    const QString path = temp.filePath(QStringLiteral("scripted-gdb.sh"));
    QFile script(path);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};

    static const char source[] = R"SH(#!/bin/sh
printf '(gdb)\r\n'
stop_round=0
while IFS= read -r line; do
  printf '%s\n' "$line" >> "$LOG_PATH"
  token=$(printf '%s\n' "$line" | sed 's/[^0-9].*$//')
  command=${line#"$token"}
  case "$command" in
    -file-exec-and-symbols*)
      printf '%s^done\n' "$token"
      ;;
    -thread-info)
      printf '%s^done,threads=[{id="1",name="main",state="stopped"}],current-thread-id="1"\n' "$token"
      ;;
    -stack-list-frames)
      printf '%s^done,stack=[frame={level="0",func="main",fullname="e2e.c",line="%s"}]\n' "$token" "$((stop_round + 10))"
      ;;
    -stack-select-frame*)
      printf '%s^done\n' "$token"
      ;;
    -stack-list-variables*)
      printf '%s^done,variables=[{name="x",value="%s",type="int"},{name="cfg",value="{mode = %s, volt = 48}",type="Config"},{name="p",value="0x2000",type="Node *"}]\n' "$token" "$((stop_round * 10))" "$stop_round"
      ;;
    '-data-evaluate-expression "&x"')
      printf '%s^done,value="0x1000"\n' "$token"
      ;;
    '-data-evaluate-expression "&cfg"')
      printf '%s^done,value="0x1100"\n' "$token"
      ;;
    '-data-evaluate-expression "&p"')
      printf '%s^done,value="0x1200"\n' "$token"
      ;;
    -test-stop)
      stop_round=$((stop_round + 1))
      printf '%s^done\n*stopped,reason="breakpoint-hit",thread-id="1",frame={func="main",fullname="e2e.c",line="%s",addr="0x4000"}\n' "$token" "$((stop_round + 10))"
      ;;
    *)
      printf '%s^done,value="default"\n' "$token"
      ;;
  esac
done
)SH";
    QString expanded = QString::fromLatin1(source);
    expanded.replace(QStringLiteral("$LOG_PATH"), logPath);
    script.write(expanded.toUtf8());
    script.close();
    if (!script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                               | QFileDevice::ExeOwner))
        return {};
    return path;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir temp;
    if (!temp.isValid())
        return 1;
    const QString logPath = temp.filePath(QStringLiteral("mi-commands.log"));
    const QString fakeGdb = createScriptedGdb(temp, logPath);
    if (fakeGdb.isEmpty())
        return 2;
    const QString inferior = temp.filePath(QStringLiteral("inferior.elf"));
    QFile inferiorFile(inferior);
    if (!inferiorFile.open(QIODevice::WriteOnly) || inferiorFile.write("fake") != 4)
        return 3;
    inferiorFile.close();

    GdbMiSession session;
    session.setBackend(DebuggerSession::Backend::GdbMi);
    session.setGdbExecutable(fakeGdb);
    session.setCommandTimeoutMs(10000);
    session.setReverseMode(DebuggerSession::ReverseMode::Disabled);

    // Production wiring, mirroring the UI layer.
    HistorySession history;
    GdbStopHistoryBackend backend(&session);
    backend.attachStore(&history.eventStore());
    history.setBackend(&backend);
    history.setSnapshotResolver([&](int stepIndex) -> std::optional<ExecutionSnapshot> {
        for (const ExecutionSnapshot &snapshot : session.executionHistory()) {
            if (snapshot.stepIndex == stepIndex)
                return snapshot;
        }
        return std::nullopt;
    });
    QObject::connect(&backend, &GdbStopHistoryBackend::stopRecorded,
                     [&](const TraceEvent &event) { history.addEvent(event); });
    DisplayedStateModel display(&session, &history);

    int starts = 0;
    QObject::connect(&session, &DebuggerSession::targetStarted, [&] { ++starts; });

    session.startSession(inferior);
    if (!waitFor([&] { return starts > 0; }))
        return 4;

    // Three stops: x = 10 / 20 / 30 at lines 11 / 12 / 13.
    for (int stop = 1; stop <= 3; ++stop) {
        session.sendRawCommand(QStringLiteral("-test-stop"));
        if (!waitFor([&] { return session.executionHistory().size() == stop; }, 15000))
            return 10 + stop;
    }

    // Real async ordering produced a 1:1 event<->snapshot link by stepIndex.
    CHECK(history.storedEvents().size() == 3);
    CHECK(session.executionHistory().size() == 3);
    for (int i = 0; i < 3; ++i) {
        const TraceEvent &event = history.storedEvents()[size_t(i)];
        const ExecutionSnapshot &snapshot = session.executionHistory()[i];
        CHECK(traceSnapshotStep(event).value_or(-1) == snapshot.stepIndex);
        const auto resolved = history.snapshotForEvent(event.id);
        CHECK(resolved.has_value());
        CHECK(resolved->variableValues.value(QStringLiteral("x"))
              == snapshot.variableValues.value(QStringLiteral("x")));
    }
    CHECK(session.executionHistory()[0].variableValues.value(QStringLiteral("x"))
          == QStringLiteral("10"));
    CHECK(session.executionHistory()[2].variableValues.value(QStringLiteral("x"))
          == QStringLiteral("30"));

    // Recording preserves variable structure: struct children and pointers
    // are captured under their full paths, never flattened away.
    for (int i = 0; i < 3; ++i) {
        const auto &values = session.executionHistory()[i].variableValues;
        CHECK(values.contains(QStringLiteral("cfg")));
        CHECK(values.contains(QStringLiteral("cfg.mode")));
        CHECK(values.contains(QStringLiteral("cfg.volt")));
        CHECK(values.contains(QStringLiteral("p")));
        CHECK(values.value(QStringLiteral("cfg.mode")) == QString::number(i + 1));
        CHECK(values.value(QStringLiteral("cfg.volt")) == QStringLiteral("48"));
        CHECK(values.value(QStringLiteral("p")) == QStringLiteral("0x2000"));
    }

    // Live shows the latest stop.
    CHECK(display.displayedState().isLive());
    CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
          == QStringLiteral("30"));
    CHECK(!history.canSelectNextEvent());

    // Sequence: Live -> previous stop -> previous stop -> next stop -> Go Live.
    // Every step shows that stop's full structured snapshot, never live data.
    CHECK(history.selectPreviousEvent());
    CHECK(!display.displayedState().isLive());
    CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
          == QStringLiteral("20"));
    CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("cfg.mode"))
          == QStringLiteral("2"));

    CHECK(history.selectPreviousEvent());
    CHECK(!display.displayedState().isLive());
    CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
          == QStringLiteral("10"));
    CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("cfg.mode"))
          == QStringLiteral("1"));
    CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("p"))
          == QStringLiteral("0x2000"));
    CHECK(display.displayedState().snapshot->line == 11);
    CHECK(display.displayedState().snapshot->file == QStringLiteral("e2e.c"));
    CHECK(!history.canSelectPreviousEvent());
    CHECK(history.canSelectNextEvent());

    CHECK(history.selectNextEvent());
    CHECK(!display.displayedState().isLive());
    CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
          == QStringLiteral("20"));
    CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("cfg.mode"))
          == QStringLiteral("2"));

    // Stepping past the last recorded stop returns to Live, not to a copy.
    CHECK(history.selectNextEvent());
    CHECK(display.displayedState().isLive());
    CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
          == QStringLiteral("30"));
    CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("cfg.mode"))
          == QStringLiteral("3"));

    // Explicit Go Live from history restores the latest/current presentation.
    history.selectPreviousEvent();
    CHECK(!display.displayedState().isLive());
    display.goLive();
    CHECK(display.displayedState().isLive());
    CHECK(display.displayedState().snapshot->variableValues.value(QStringLiteral("x"))
          == QStringLiteral("30"));

    // Browsing history never commanded the target: no -exec-* traffic.
    session.terminateSession();
    QFile log(logPath);
    if (!log.open(QIODevice::ReadOnly | QIODevice::Text))
        return 5;
    const QString commands = QString::fromUtf8(log.readAll());
    CHECK(!commands.contains(QStringLiteral("-exec-continue")));
    CHECK(!commands.contains(QStringLiteral("-exec-step")));
    CHECK(!commands.contains(QStringLiteral("-exec-next")));
    CHECK(!commands.contains(QStringLiteral("-exec-finish")));
    CHECK(!commands.contains(QStringLiteral("-exec-interrupt")));

    std::cout << "history stop-flow tests passed\n";
    return 0;
}
