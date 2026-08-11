#include "RuntimeObjectGraph.h"

namespace {
QString normalizedAddress(QString address)
{
	address = address.trimmed().toLower();
	bool ok = false;
	const qulonglong numeric = address.toULongLong(&ok, 0);
	return ok ? QStringLiteral("0x%1").arg(numeric, 0, 16) : address;
}
}

QString RuntimeObjectGraph::identityFor(const QString& address, const QString& type,
	                                     const QString& fallbackExpression)
{
	const QString normalized = normalizedAddress(address);
	if (!normalized.isEmpty())
		return QStringLiteral("object:%1:%2").arg(normalized, type.trimmed());
	return QStringLiteral("expression:%1:%2").arg(fallbackExpression, type.trimmed());
}

QString RuntimeObjectGraph::referenceIdentity(const QString& sourceObjectId,
	                                           const QString& sourceExpression)
{
	return sourceObjectId + QStringLiteral("::") + sourceExpression;
}

RuntimeObject& RuntimeObjectGraph::ensureObject(const QString& address, const QString& type,
	                                            const QString& fallbackExpression)
{
	const QString id = identityFor(address, type, fallbackExpression);
	auto it = m_objects.find(id);
	if (it == m_objects.end()) {
		auto value = std::make_unique<RuntimeObject>();
		value->id = id;
		value->address = normalizedAddress(address);
		value->type = type.trimmed();
		it = m_objects.emplace(id, std::move(value)).first;
	}
	return *it->second;
}

void RuntimeObjectGraph::setMember(const QString& objectId, const QString& expression,
	                                const QString& value, const QString& type)
{
	auto it = m_objects.find(objectId);
	if (it == m_objects.end()) return;
	it->second->members.insert(expression, RuntimeMember{expression, value, type});
}

void RuntimeObjectGraph::setReference(const QString& sourceObjectId, const QString& expression,
	                                   const QString& destinationObjectId)
{
	const QString id = referenceIdentity(sourceObjectId, expression);
	m_references.insert(id, RuntimeReference{id, sourceObjectId, expression, destinationObjectId});
}

const RuntimeObject* RuntimeObjectGraph::object(const QString& id) const
{
	auto it = m_objects.find(id);
	return it == m_objects.end() ? nullptr : it->second.get();
}

const RuntimeReference* RuntimeObjectGraph::reference(const QString& id) const
{
	auto it = m_references.constFind(id);
	return it == m_references.cend() ? nullptr : &*it;
}

RuntimeGraphDiff diffRuntimeGraphs(const RuntimeObjectGraph& before,
	                                const RuntimeObjectGraph& after)
{
	RuntimeGraphDiff diff;
	for (const auto& entry : after.objects()) {
		const RuntimeObject* oldObject = before.object(entry.first);
		if (!oldObject) { diff.addedObjects.insert(entry.first); continue; }
		for (auto member = entry.second->members.cbegin(); member != entry.second->members.cend(); ++member) {
			auto oldMember = oldObject->members.constFind(member.key());
			if (oldMember == oldObject->members.cend() || oldMember->value != member->value || oldMember->type != member->type) {
				diff.modifiedObjects.insert(entry.first);
				diff.modifiedMembers[entry.first].insert(member.key());
			}
		}
	}
	for (const auto& entry : before.objects())
		if (!after.object(entry.first)) diff.removedObjects.insert(entry.first);

	for (auto it = after.references().cbegin(); it != after.references().cend(); ++it) {
		const RuntimeReference* oldReference = before.reference(it.key());
		if (!oldReference) diff.addedReferences.insert(it.key());
		else if (oldReference->destinationObjectId != it->destinationObjectId) {
			diff.retargetedReferences.insert(it.key());
			// Retargeting is both the disappearance of the old directed edge and
			// the appearance of the new one, while retaining logical identity.
			diff.removedReferences.insert(it.key());
			diff.addedReferences.insert(it.key());
		}
	}
	for (auto it = before.references().cbegin(); it != before.references().cend(); ++it)
		if (!after.reference(it.key())) diff.removedReferences.insert(it.key());
	return diff;
}
