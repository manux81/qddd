#include "GdbStopHistoryBackend.h"

#include "DebugSession.h"
#include "TimelineTrack.h"

#include <QDateTime>

namespace qddd {
namespace history {

GdbStopHistoryBackend::GdbStopHistoryBackend(DebuggerSession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    if (!m_session)
        return;
    connect(m_session, &DebuggerSession::stoppedAt, this,
            &GdbStopHistoryBackend::onStoppedAt);
    connect(m_session, &DebuggerSession::stoppedAtAddress, this,
            &GdbStopHistoryBackend::onStoppedAtAddress);
    connect(m_session, &DebuggerSession::targetStarted, this,
            &GdbStopHistoryBackend::onTargetStarted);
    connect(m_session, &DebuggerSession::targetExited, this,
            &GdbStopHistoryBackend::onTargetExited);
    connect(m_session, &DebuggerSession::targetStopped, this, [this] {
        m_sessionActive = true;
        emit backendChanged();
    });
}

bool GdbStopHistoryBackend::isAvailable() const
{
    return m_session && m_sessionActive;
}

HistoryCapabilities GdbStopHistoryBackend::capabilities() const
{
    // Stop recording only. Everything else needs a future replay backend.
    return HistoryCapability::Recording;
}

std::optional<ExecutionState> GdbStopHistoryBackend::currentState()
{
    if (!m_session)
        return std::nullopt;
    ExecutionState state;
    state.time = nowNs();
    if (!m_lastAddress.isEmpty()) {
        bool ok = false;
        const quint64 pc = m_lastAddress.toULongLong(&ok, 0);
        if (ok) {
            state.hasPc = true;
            state.pc = pc;
        }
    }
    return state;
}

bool GdbStopHistoryBackend::seek(TimePoint) { return false; }
bool GdbStopHistoryBackend::stepForward() { return false; }
bool GdbStopHistoryBackend::stepBackward() { return false; }
bool GdbStopHistoryBackend::continueForward() { return false; }
bool GdbStopHistoryBackend::continueBackward() { return false; }

std::vector<TraceEvent> GdbStopHistoryBackend::events(TimeRange range)
{
    std::vector<TraceEvent> out;
    if (!m_store || !range.isValid())
        return out;
    for (const TraceEvent &event : *m_store) {
        if (range.contains(event.time))
            out.push_back(event);
    }
    return out;
}

void GdbStopHistoryBackend::attachStore(std::vector<TraceEvent> *store)
{
    m_store = store;
}

void GdbStopHistoryBackend::onStoppedAt(const QString &file, int line, const QString &function)
{
    m_sessionActive = true;
    TraceEvent event;
    event.id = m_nextId++;
    event.time = nowNs();
    event.type = TraceEventType::Stop;
    event.trackId = QString::fromLatin1(TrackIds::Stops);
    // Deterministic event<->snapshot link: the counter was already
    // incremented for this stop before stoppedAt was emitted, and the
    // snapshot captured for this stop records the same value, so no timing
    // assumptions are needed (snapshot capture is async).
    event.metadata.insert(QString::fromLatin1(SnapshotStepKey),
                          m_session->lastStopStepIndex());
    if (!m_lastAddress.isEmpty()) {
        bool ok = false;
        const quint64 pc = m_lastAddress.toULongLong(&ok, 0);
        if (ok)
            event.address = pc;
    }
    if (!file.isEmpty() && line > 0) {
        SourceLocation loc;
        loc.file = file;
        loc.line = line;
        loc.function = function;
        event.source = loc;
    }
    emit stopRecorded(event);
    emit backendChanged();
}

void GdbStopHistoryBackend::onStoppedAtAddress(const QString &address)
{
    m_lastAddress = address.trimmed();
}

void GdbStopHistoryBackend::onTargetStarted()
{
    m_sessionActive = true;
    m_lastAddress.clear();
    emit backendChanged();
}

void GdbStopHistoryBackend::onTargetExited()
{
    m_sessionActive = false;
    emit backendChanged();
}

TimePoint GdbStopHistoryBackend::nowNs()
{
    return QDateTime::currentMSecsSinceEpoch() * 1000000;
}

} // namespace history
} // namespace qddd
