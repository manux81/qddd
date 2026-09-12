#pragma once
#include <QLoggingCategory>
// Disabled debug-level traces can be enabled selectively through QT_LOGGING_RULES.
inline const QLoggingCategory& debuggerTransportLog() { static QLoggingCategory c("qddd.transport",QtWarningMsg); return c; }
inline const QLoggingCategory& debuggerProtocolLog() { static QLoggingCategory c("qddd.protocol",QtWarningMsg); return c; }
inline const QLoggingCategory& debuggerSessionLog() { static QLoggingCategory c("qddd.session",QtWarningMsg); return c; }
inline const QLoggingCategory& debuggerGraphLog() { static QLoggingCategory c("qddd.graph",QtWarningMsg); return c; }
inline const QLoggingCategory& debuggerUiLog() { static QLoggingCategory c("qddd.ui",QtWarningMsg); return c; }
inline const QLoggingCategory& debuggerMemoryLog() { static QLoggingCategory c("qddd.memory",QtWarningMsg); return c; }
inline const QLoggingCategory& debuggerSnapshotsLog() { static QLoggingCategory c("qddd.snapshots",QtWarningMsg); return c; }
