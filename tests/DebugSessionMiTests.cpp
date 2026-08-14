#include "DebugSession.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDevice>
#include <QTemporaryDir>
#include <QThread>

#include <functional>

namespace {

bool waitFor(const std::function<bool()>& predicate, int timeoutMs = 3000)
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

QString createFakeGdb(QTemporaryDir& temp)
{
	const QString path = temp.filePath(QStringLiteral("fake-gdb.sh"));
	QFile script(path);
	if (!script.open(QIODevice::WriteOnly | QIODevice::Text))
		return {};

	static const char source[] = R"SH(#!/bin/sh
printf '(gdb)\r\n'
while IFS= read -r line; do
  token=$(printf '%s\n' "$line" | sed 's/[^0-9].*$//')
  command=${line#"$token"}
  case "$command" in
    -file-exec-and-symbols*)
      printf '%s^do' "$token"
      sleep 0.03
      printf 'ne\r\n'
      ;;
    -test-multi)
      printf '~"hello\\n"\n=thread-created,id="1"\r\n%s^done,value="ok"\n' "$token"
      ;;
    -test-error)
      printf '%s^error,msg="expected failure"\n' "$token"
      ;;
    -test-escape-long)
      payload=$(awk 'BEGIN { for (i=0; i<8192; ++i) printf "x" }')
      value="line\\n${payload}\\\"quoted\\\\path"
      printf '%s^done,value="%s"\r\n' "$token" "$value"
      ;;
    -test-crash)
      printf '%s^do' "$token"
      exit 7
      ;;
    -test-timeout)
      ;;
    -test-mismatch)
      wrong=$((token + 1))
      printf '%s^done\n' "$wrong"
      ;;
    *)
      printf '%s^done,value="default"\n' "$token"
      ;;
  esac
done
)SH";
	script.write(source);
	script.close();
	if (!script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
	                           QFileDevice::ExeOwner))
		return {};
	return path;
}

bool start(DebuggerSession& session, const QString& executable, int& starts)
{
	const int previous = starts;
	session.startSession(executable);
	return waitFor([&] { return starts > previous; });
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	DebugVariable pointerRoot;
	pointerRoot.name = QStringLiteral("ptr");
	pointerRoot.isPointer = true;
	DebugVariable pointerMember;
	pointerMember.name = QStringLiteral("field");
	pointerMember.parent = &pointerRoot;
	if (pointerMember.fullPath() != QStringLiteral("(*(ptr)).field"))
		return 23;
	if (formatDebugValue(QStringLiteral("42"), DebugValueFormat::Hexadecimal) !=
	    QStringLiteral("0x2a"))
		return 24;
	if (formatDebugValue(QStringLiteral("0x2a"), DebugValueFormat::Decimal) !=
	    QStringLiteral("42"))
		return 25;
	if (formatDebugValue(QStringLiteral("-10"), DebugValueFormat::Binary) !=
	    QStringLiteral("-0b1010"))
		return 26;
	if (formatDebugValue(QStringLiteral("65"), DebugValueFormat::Character) !=
	    QStringLiteral("'A' (65)"))
		return 27;

	QTemporaryDir temp;
	if (!temp.isValid())
		return 1;

	const QString fakeGdb = createFakeGdb(temp);
	if (fakeGdb.isEmpty())
		return 2;
	const QString inferior = temp.filePath(QStringLiteral("inferior.elf"));
	QFile inferiorFile(inferior);
	if (!inferiorFile.open(QIODevice::WriteOnly) || inferiorFile.write("fake") != 4)
		return 3;
	inferiorFile.close();

	DebuggerSession session;
	session.setBackend(DebuggerSession::Backend::GdbMi);
	session.setGdbExecutable(fakeGdb);
	session.setCommandTimeoutMs(1000);

	int starts = 0;
	int exits = 0;
	int startFailures = 0;
	bool consoleEscapeDecoded = false;
	QString diagnostics;
	QObject::connect(&session, &DebuggerSession::targetStarted,
	                 [&] { ++starts; });
	QObject::connect(&session, &DebuggerSession::targetExited,
	                 [&](int) { ++exits; });
	QObject::connect(&session, &DebuggerSession::targetStartFailed,
	                 [&](const QString&) { ++startFailures; });
	QObject::connect(&session, &DebuggerSession::debuggerOutput,
	                 [&](const QString& text) {
		                 diagnostics += text;
		                 if (text == QStringLiteral("hello\n"))
			                 consoleEscapeDecoded = true;
	                 });

	if (!start(session, inferior, starts))
		return 4;

	bool multiDone = false;
	session.sendRawCommand(QStringLiteral("-test-multi"),
	                       [&](const QString& reply) {
		                       multiDone = reply.contains(QStringLiteral("^done,value=\"ok\""));
	                       });
	if (!waitFor([&] { return multiDone; }) || !consoleEscapeDecoded)
		return 5;

	session.addWatchExpression(QStringLiteral("watched"));
	auto findWatch = [&session](const QString& expression) -> const DebugVariable* {
		for (const auto& variable : session.variables())
			if (variable && variable->isWatch && variable->name == expression)
				return variable.get();
		return nullptr;
	};
	if (!waitFor([&] { return findWatch(QStringLiteral("watched")) != nullptr; }))
		return 17;
	if (!session.isWatchExpressionEnabled(QStringLiteral("watched")))
		return 18;
	session.setValueFormat(QStringLiteral("watched"), DebugValueFormat::Hexadecimal);
	if (session.valueFormat(QStringLiteral("watched")) != DebugValueFormat::Hexadecimal)
		return 28;

	session.setWatchExpressionEnabled(QStringLiteral("watched"), false);
	const DebugVariable* disabledWatch = findWatch(QStringLiteral("watched"));
	if (!disabledWatch || disabledWatch->enabled ||
	    session.isWatchExpressionEnabled(QStringLiteral("watched")))
		return 19;

	session.replaceWatchExpression(QStringLiteral("watched"),
	                               QStringLiteral("replacement"));
	if (session.watchExpressions().contains(QStringLiteral("watched")) ||
	    !session.watchExpressions().contains(QStringLiteral("replacement")) ||
	    session.isWatchExpressionEnabled(QStringLiteral("replacement")) ||
	    session.valueFormat(QStringLiteral("replacement")) != DebugValueFormat::Hexadecimal)
		return 20;

	session.setWatchExpressionEnabled(QStringLiteral("replacement"), true);
	if (!waitFor([&] {
		const DebugVariable* watch = findWatch(QStringLiteral("replacement"));
		return watch && watch->enabled;
	}))
		return 21;
	session.removeWatchExpression(QStringLiteral("replacement"));
	if (findWatch(QStringLiteral("replacement")) ||
	    !session.watchExpressions().isEmpty())
		return 22;

	bool errorDone = false;
	bool afterErrorDone = false;
	session.sendRawCommand(QStringLiteral("-test-error"),
	                       [&](const QString& reply) { errorDone = reply.contains("^error"); });
	session.sendRawCommand(QStringLiteral("-test-after-error"),
	                       [&](const QString&) { afterErrorDone = true; });
	if (!waitFor([&] { return errorDone && afterErrorDone; }))
		return 6;

	bool longDone = false;
	session.sendRawCommand(QStringLiteral("-test-escape-long"),
	                       [&](const QString& reply) {
		                       longDone = reply.size() > 8000 &&
		                                  reply.contains(QStringLiteral("\\n")) &&
		                                  reply.contains(QStringLiteral("\\\"quoted\\\\path"));
	                       });
	if (!waitFor([&] { return longDone; }))
		return 7;

	bool staleCrashCallback = false;
	const int exitsBeforeCrash = exits;
	session.sendRawCommand(QStringLiteral("-test-crash"),
	                       [&](const QString&) { staleCrashCallback = true; });
	if (!waitFor([&] { return exits > exitsBeforeCrash; }) || staleCrashCallback ||
	    !diagnostics.contains(QStringLiteral("incomplete MI record")))
		return 8;

	if (!start(session, inferior, starts))
		return 9;
	bool restartDone = false;
	session.sendRawCommand(QStringLiteral("-test-restarted"),
	                       [&](const QString&) { restartDone = true; });
	if (!waitFor([&] { return restartDone; }) || staleCrashCallback)
		return 10;

	bool queuedCallback = false;
	session.sendRawCommand(QStringLiteral("-test-timeout"));
	session.sendRawCommand(QStringLiteral("-test-queued"),
	                       [&](const QString&) { queuedCallback = true; });
	session.terminateSession();
	session.terminateSession();
	if (session.isRunning() || queuedCallback)
		return 11;

	session.setGdbExecutable(QStringLiteral("/definitely/missing/qddd-gdb"));
	session.startSession(inferior);
	if (!waitFor([&] { return startFailures > 0; }))
		return 12;
	session.setGdbExecutable(fakeGdb);
	if (!start(session, inferior, starts))
		return 13;

	session.setCommandTimeoutMs(100);
	bool afterTimeout = false;
	const int exitsBeforeTimeout = exits;
	session.sendRawCommand(QStringLiteral("-test-timeout"));
	session.sendRawCommand(QStringLiteral("-test-after-timeout"),
	                       [&](const QString&) { afterTimeout = true; });
	if (!waitFor([&] { return exits > exitsBeforeTimeout; }) || afterTimeout ||
	    !diagnostics.contains(QStringLiteral("timed out after 100 ms")))
		return 14;

	session.setCommandTimeoutMs(1000);
	if (!start(session, inferior, starts))
		return 15;
	const int exitsBeforeMismatch = exits;
	session.sendRawCommand(QStringLiteral("-test-mismatch"));
	if (!waitFor([&] { return exits > exitsBeforeMismatch; }) ||
	    !diagnostics.contains(QStringLiteral("unexpected MI result token")))
		return 16;

	session.terminateSession();
	return 0;
}
