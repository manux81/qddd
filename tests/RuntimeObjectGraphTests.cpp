#include "RuntimeObjectGraph.h"
#include <QCoreApplication>
#include <cstdlib>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition "\n"; return EXIT_FAILURE; } } while (false)

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	RuntimeObjectGraph aliases;
	auto& x1 = aliases.ensureObject("0x1000", "Node", "a");
	auto& x2 = aliases.ensureObject("0X00001000", "Node", "b");
	CHECK(x1.id == x2.id); CHECK(aliases.objects().size() == 1);
	aliases.setReference("root", "a", x1.id); aliases.setReference("root", "b", x1.id);
	CHECK(aliases.references().size() == 2);

	RuntimeObjectGraph chain;
	auto& a = chain.ensureObject("0x1", "Node");
	auto& b = chain.ensureObject("0x2", "Node");
	auto& c = chain.ensureObject("0x3", "Node");
	chain.setReference(a.id, "next", b.id); chain.setReference(b.id, "next", c.id);
	CHECK(chain.objects().size() == 3); CHECK(chain.references().size() == 2);

	RuntimeObjectGraph cycle;
	auto& ca = cycle.ensureObject("0xa", "Node"); auto& cb = cycle.ensureObject("0xb", "Node");
	cycle.setReference(ca.id, "next", cb.id); cycle.setReference(cb.id, "next", ca.id);
	CHECK(cycle.objects().size() == 2); CHECK(cycle.references().size() == 2);

	RuntimeObjectGraph shared;
	auto& root = shared.ensureObject("0x10", "Pair"); auto& child = shared.ensureObject("0x20", "Node");
	shared.setReference(root.id, "left", child.id); shared.setReference(root.id, "right", child.id);
	CHECK(shared.objects().size() == 2); CHECK(shared.references().size() == 2);

	RuntimeObjectGraph before; auto& oldA = before.ensureObject("0x1", "Node"); auto& oldB = before.ensureObject("0x2", "Node");
	before.setReference(oldA.id, "next", oldB.id); before.setMember(oldA.id, "value", "10", "int");
	RuntimeObjectGraph after; auto& newA = after.ensureObject("0x1", "Node"); auto& newC = after.ensureObject("0x3", "Node");
	after.setReference(newA.id, "next", newC.id); after.setMember(newA.id, "value", "11", "int");
	auto diff = diffRuntimeGraphs(before, after);
	const QString nextId = RuntimeObjectGraph::referenceIdentity(oldA.id, "next");
	CHECK(diff.retargetedReferences.contains(nextId));
	CHECK(diff.removedReferences.contains(nextId)); CHECK(diff.addedReferences.contains(nextId));
	CHECK(diff.modifiedMembers[oldA.id].contains("value"));
	CHECK(!diff.addedObjects.contains(oldA.id)); CHECK(diff.addedObjects.contains(newC.id));
	return EXIT_SUCCESS;
}
