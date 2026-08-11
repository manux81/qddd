#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <memory>
#include <map>

enum class RuntimeChangeState { Unchanged, Added, Modified, Removed };

struct RuntimeMember
{
	QString expression;
	QString value;
	QString type;
	RuntimeChangeState change = RuntimeChangeState::Unchanged;
};

struct RuntimeReference
{
	QString id;
	QString sourceObjectId;
	QString sourceExpression;
	QString destinationObjectId;
	RuntimeChangeState change = RuntimeChangeState::Unchanged;
};

struct RuntimeObject
{
	QString id;
	QString address;
	QString type;
	QHash<QString, RuntimeMember> members;
	RuntimeChangeState change = RuntimeChangeState::Unchanged;
};

// Owned, UI-independent snapshot of runtime objects and logical references.
// Addresses are normalized and combined with the concrete type to avoid
// duplicating an object merely because it was reached through another path.
class RuntimeObjectGraph
{
public:
	static QString identityFor(const QString& address, const QString& type,
	                           const QString& fallbackExpression = {});
	static QString referenceIdentity(const QString& sourceObjectId,
	                                 const QString& sourceExpression);

	RuntimeObject& ensureObject(const QString& address, const QString& type,
	                            const QString& fallbackExpression = {});
	void setMember(const QString& objectId, const QString& expression,
	               const QString& value, const QString& type = {});
	void setReference(const QString& sourceObjectId, const QString& expression,
	                  const QString& destinationObjectId);

	const RuntimeObject* object(const QString& id) const;
	const RuntimeReference* reference(const QString& id) const;
	const std::map<QString, std::unique_ptr<RuntimeObject>>& objects() const { return m_objects; }
	const QHash<QString, RuntimeReference>& references() const { return m_references; }

private:
	std::map<QString, std::unique_ptr<RuntimeObject>> m_objects;
	QHash<QString, RuntimeReference> m_references;
};

struct RuntimeGraphDiff
{
	QSet<QString> addedObjects;
	QSet<QString> removedObjects;
	QSet<QString> modifiedObjects;
	QSet<QString> addedReferences;
	QSet<QString> removedReferences;
	QSet<QString> retargetedReferences;
	QHash<QString, QSet<QString>> modifiedMembers;
};

RuntimeGraphDiff diffRuntimeGraphs(const RuntimeObjectGraph& before,
	                                const RuntimeObjectGraph& after);
