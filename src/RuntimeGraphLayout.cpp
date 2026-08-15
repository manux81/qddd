/*
 * Copyright (c) 2026, Manuele Conti
 * All rights reserved.
 */

#include "RuntimeGraphLayout.h"

#include <QRectF>
#include <QSet>
#include <QtGlobal>

#include <algorithm>
#include <functional>
#include <limits>

namespace {

using Adjacency = QHash<QString, QVector<QString>>;

QVector<QString> sortedUnique(QVector<QString> values)
{
	std::sort(values.begin(), values.end());
	values.erase(std::unique(values.begin(), values.end()), values.end());
	return values;
}

struct TarjanState
{
	const Adjacency& outgoing;
	QHash<QString, int> index;
	QHash<QString, int> lowLink;
	QSet<QString> onStack;
	QVector<QString> stack;
	QVector<QVector<QString>> components;
	int nextIndex = 0;

	void visit(const QString& id)
	{
		index[id] = nextIndex;
		lowLink[id] = nextIndex;
		++nextIndex;
		stack.push_back(id);
		onStack.insert(id);

		for (const QString& destination : outgoing.value(id)) {
			if (!index.contains(destination)) {
				visit(destination);
				lowLink[id] = qMin(lowLink.value(id), lowLink.value(destination));
			} else if (onStack.contains(destination)) {
				lowLink[id] = qMin(lowLink.value(id), index.value(destination));
			}
		}

		if (lowLink.value(id) != index.value(id))
			return;

		QVector<QString> component;
		while (!stack.isEmpty()) {
			const QString current = stack.takeLast();
			onStack.remove(current);
			component.push_back(current);
			if (current == id)
				break;
		}
		components.push_back(sortedUnique(std::move(component)));
	}
};

QVector<QVector<QString>> weakComponents(const QVector<QString>& ids,
	                                      const Adjacency& undirected)
{
	QVector<QVector<QString>> result;
	QSet<QString> visited;
	for (const QString& start : ids) {
		if (visited.contains(start))
			continue;
		QVector<QString> component;
		QVector<QString> queue{start};
		visited.insert(start);
		for (int cursor = 0; cursor < queue.size(); ++cursor) {
			const QString current = queue[cursor];
			component.push_back(current);
			for (const QString& neighbour : undirected.value(current)) {
				if (!visited.contains(neighbour)) {
					visited.insert(neighbour);
					queue.push_back(neighbour);
				}
			}
		}
		result.push_back(sortedUnique(std::move(component)));
	}
	return result;
}

qreal nodeWidth(const RuntimeLayoutNode& node)
{
	return qMax<qreal>(1.0, node.size.width());
}

qreal nodeHeight(const RuntimeLayoutNode& node)
{
	return qMax<qreal>(1.0, node.size.height());
}

struct LocalLayout
{
	QHash<QString, QPointF> positions;
	QHash<QString, int> layers;
	QSizeF size;
};

LocalLayout layoutComponent(const QVector<QString>& componentIds,
	                         const QHash<QString, RuntimeLayoutNode>& nodes,
	                         const Adjacency& outgoing,
	                         const Adjacency& incoming,
	                         const RuntimeLayoutOptions& options)
{
	TarjanState tarjan{outgoing};
	for (const QString& id : componentIds)
		if (!tarjan.index.contains(id))
			tarjan.visit(id);

	std::sort(tarjan.components.begin(), tarjan.components.end(),
	          [](const QVector<QString>& a, const QVector<QString>& b) {
		          return a.value(0) < b.value(0);
	          });

	QHash<QString, int> sccForNode;
	for (int i = 0; i < tarjan.components.size(); ++i)
		for (const QString& id : tarjan.components[i])
			sccForNode[id] = i;

	QVector<QSet<int>> dagSets(tarjan.components.size());
	QVector<int> indegree(tarjan.components.size(), 0);
	for (const QString& source : componentIds) {
		const int from = sccForNode.value(source);
		for (const QString& destination : outgoing.value(source)) {
			if (!sccForNode.contains(destination))
				continue;
			const int to = sccForNode.value(destination);
			if (from != to && !dagSets[from].contains(to)) {
				dagSets[from].insert(to);
				++indegree[to];
			}
		}
	}

	QVector<int> layerForScc(tarjan.components.size(), 0);
	QVector<int> ready;
	for (int i = 0; i < indegree.size(); ++i)
		if (indegree[i] == 0)
			ready.push_back(i);
	std::sort(ready.begin(), ready.end());
	for (int cursor = 0; cursor < ready.size(); ++cursor) {
		const int from = ready[cursor];
		QVector<int> destinations = dagSets[from].values().toVector();
		std::sort(destinations.begin(), destinations.end());
		for (int to : destinations) {
			layerForScc[to] = qMax(layerForScc[to], layerForScc[from] + 1);
			if (--indegree[to] == 0) {
				ready.push_back(to);
				std::sort(ready.begin() + cursor + 1, ready.end());
			}
		}
	}

	int maxLayer = 0;
	QHash<QString, int> layerForNode;
	for (const QString& id : componentIds) {
		const int layer = layerForScc.value(sccForNode.value(id));
		layerForNode[id] = layer;
		maxLayer = qMax(maxLayer, layer);
	}

	QVector<QVector<QString>> layerNodes(maxLayer + 1);
	for (const QString& id : componentIds)
		layerNodes[layerForNode.value(id)].push_back(id);
	for (QVector<QString>& layer : layerNodes) {
		std::sort(layer.begin(), layer.end(), [&](const QString& a, const QString& b) {
			const RuntimeLayoutNode& na = nodes[a];
			const RuntimeLayoutNode& nb = nodes[b];
			if (na.hasPreviousPosition != nb.hasPreviousPosition)
				return na.hasPreviousPosition;
			if (na.hasPreviousPosition && na.previousPosition.y() != nb.previousPosition.y())
				return na.previousPosition.y() < nb.previousPosition.y();
			return a < b;
		});
	}

	// Replace edges spanning multiple layers with deterministic virtual nodes.
	// They participate only in ordering/alignment and are never returned to the
	// caller. This is the useful Sugiyama idea behind DDD's edge hints, adapted
	// independently to our variable-sized card layout.
	Adjacency layoutOutgoing;
	Adjacency layoutIncoming;
	QSet<QString> virtualNodes;
	int nextVirtualId = 0;
	for (const QString& source : componentIds) {
		for (const QString& destination : outgoing.value(source)) {
			if (!layerForNode.contains(destination))
				continue;
			const int sourceLayer = layerForNode.value(source);
			const int destinationLayer = layerForNode.value(destination);
			QString previous = source;
			if (options.useLongEdgeHints && destinationLayer - sourceLayer > 1) {
				for (int layer = sourceLayer + 1; layer < destinationLayer; ++layer) {
					const QString hint = QString(QChar(0x1f))
						+ QStringLiteral("layout-hint-%1").arg(nextVirtualId++, 8, 10,
						                                      QLatin1Char('0'));
					virtualNodes.insert(hint);
					layerNodes[layer].push_back(hint);
					layoutOutgoing[previous].push_back(hint);
					layoutIncoming[hint].push_back(previous);
					previous = hint;
				}
			}
			layoutOutgoing[previous].push_back(destination);
			layoutIncoming[destination].push_back(previous);
		}
	}
	for (QVector<QString>& neighbours : layoutOutgoing)
		neighbours = sortedUnique(std::move(neighbours));
	for (QVector<QString>& neighbours : layoutIncoming)
		neighbours = sortedUnique(std::move(neighbours));

	auto layoutNodeWidth = [&](const QString& id) {
		return virtualNodes.contains(id) ? 1.0 : nodeWidth(nodes[id]);
	};
	auto layoutNodeHeight = [&](const QString& id) {
		return virtualNodes.contains(id) ? 1.0 : nodeHeight(nodes[id]);
	};

	auto orderMap = [&]() {
		QHash<QString, int> order;
		for (const QVector<QString>& layer : layerNodes)
			for (int i = 0; i < layer.size(); ++i)
				order[layer[i]] = i;
		return order;
	};

	auto reorder = [&](int layerIndex, const Adjacency& neighbours,
	                   const QHash<QString, int>& order) {
		QVector<QString>& layer = layerNodes[layerIndex];
		QHash<QString, qreal> scores;
		for (const QString& id : layer) {
			qreal total = 0.0;
			int count = 0;
			for (const QString& neighbour : neighbours.value(id)) {
				if (order.contains(neighbour)) {
					total += order.value(neighbour);
					++count;
				}
			}
			if (count > 0)
				scores[id] = total / count;
		}
		std::stable_sort(layer.begin(), layer.end(), [&](const QString& a, const QString& b) {
			const bool hasA = scores.contains(a);
			const bool hasB = scores.contains(b);
			if (hasA != hasB)
				return hasA;
			if (hasA && scores.value(a) != scores.value(b))
				return scores.value(a) < scores.value(b);
			return order.value(a) < order.value(b);
		});
	};

	for (int sweep = 0; sweep < qMax(0, options.crossingReductionSweeps); ++sweep) {
		QHash<QString, int> order = orderMap();
		for (int layer = 1; layer < layerNodes.size(); ++layer) {
			reorder(layer, layoutIncoming, order);
			order = orderMap();
		}
		for (int layer = layerNodes.size() - 2; layer >= 0; --layer) {
			reorder(layer, layoutOutgoing, order);
			order = orderMap();
		}
	}

	QVector<qreal> widths(layerNodes.size(), 1.0);
	QVector<qreal> heights(layerNodes.size(), 0.0);
	for (int layer = 0; layer < layerNodes.size(); ++layer) {
		for (const QString& id : layerNodes[layer]) {
			widths[layer] = qMax(widths[layer], layoutNodeWidth(id));
			heights[layer] += layoutNodeHeight(id);
		}
		if (layerNodes[layer].size() > 1)
			heights[layer] += options.nodeSpacing * (layerNodes[layer].size() - 1);
	}
	const qreal initialComponentHeight =
		*std::max_element(heights.begin(), heights.end());

	LocalLayout result;
	qreal x = 0.0;
	for (int layer = 0; layer < layerNodes.size(); ++layer) {
		qreal y = (initialComponentHeight - heights[layer]) * 0.5;
		for (const QString& id : layerNodes[layer]) {
			result.positions[id] = QPointF(x, y);
			if (!virtualNodes.contains(id))
				result.layers[id] = layer;
			y += layoutNodeHeight(id) + options.nodeSpacing;
		}
		x += widths[layer] + options.layerSpacing;
	}

	// Pull each card towards the average centre of its neighbours while
	// preserving the barycentric order and the required vertical spacing.
	// Alternating directions avoids favouring either parents or children.
	auto alignLayer = [&](int layerIndex, const Adjacency& neighbours) {
		const QVector<QString>& layer = layerNodes[layerIndex];
		if (layer.isEmpty())
			return;
		QVector<qreal> tops;
		tops.reserve(layer.size());
		for (const QString& id : layer) {
			qreal desiredCenter = 0.0;
			int neighbourCount = 0;
			for (const QString& neighbour : neighbours.value(id)) {
				if (!result.positions.contains(neighbour))
					continue;
				desiredCenter += result.positions.value(neighbour).y()
					+ layoutNodeHeight(neighbour) * 0.5;
				++neighbourCount;
			}
			const qreal currentTop = result.positions.value(id).y();
			const qreal desiredTop = neighbourCount > 0
				? desiredCenter / neighbourCount - layoutNodeHeight(id) * 0.5
				: currentTop;
			qreal top = currentTop * 0.25 + desiredTop * 0.75;
			if (!tops.isEmpty()) {
				const int previousIndex = tops.size() - 1;
				top = qMax(top, tops.last() + layoutNodeHeight(layer[previousIndex])
				                         + options.nodeSpacing);
			}
			tops.push_back(top);
		}
		for (int i = tops.size() - 2; i >= 0; --i) {
			const qreal latestTop = tops[i + 1] - options.nodeSpacing
				- layoutNodeHeight(layer[i]);
			tops[i] = qMin(tops[i], latestTop);
		}
		for (int i = 0; i < layer.size(); ++i)
			result.positions[layer[i]].setY(tops[i]);
	};

	for (int sweep = 0; sweep < qMax(0, options.alignmentSweeps); ++sweep) {
		for (int layer = 1; layer < layerNodes.size(); ++layer)
			alignLayer(layer, layoutIncoming);
		for (int layer = layerNodes.size() - 2; layer >= 0; --layer)
			alignLayer(layer, layoutOutgoing);
	}

	qreal minY = std::numeric_limits<qreal>::max();
	qreal maxY = std::numeric_limits<qreal>::lowest();
	for (const QVector<QString>& layer : layerNodes) {
		for (const QString& id : layer) {
			minY = qMin(minY, result.positions.value(id).y());
			maxY = qMax(maxY, result.positions.value(id).y() + layoutNodeHeight(id));
		}
	}
	if (minY != std::numeric_limits<qreal>::max()) {
		for (auto it = result.positions.begin(); it != result.positions.end(); ++it)
			it.value().ry() -= minY;
	}
	const qreal componentHeight = maxY > minY ? maxY - minY : 0.0;
	result.size = QSizeF(qMax<qreal>(0.0, x - options.layerSpacing), componentHeight);
	return result;
}

} // namespace

RuntimeLayoutResult RuntimeGraphLayout::compute(
	const QVector<RuntimeLayoutNode>& inputNodes,
	const QVector<RuntimeLayoutEdge>& edges,
	const RuntimeLayoutOptions& options)
{
	RuntimeLayoutResult result;
	QHash<QString, RuntimeLayoutNode> nodes;
	for (const RuntimeLayoutNode& node : inputNodes)
		if (!node.id.isEmpty() && !nodes.contains(node.id))
			nodes.insert(node.id, node);

	QVector<QString> ids = nodes.keys().toVector();
	std::sort(ids.begin(), ids.end());
	Adjacency outgoing;
	Adjacency incoming;
	Adjacency undirected;
	for (const QString& id : ids) {
		outgoing[id] = {};
		incoming[id] = {};
		undirected[id] = {};
	}
	for (const RuntimeLayoutEdge& edge : edges) {
		if (!nodes.contains(edge.sourceId) || !nodes.contains(edge.destinationId))
			continue;
		outgoing[edge.sourceId].push_back(edge.destinationId);
		incoming[edge.destinationId].push_back(edge.sourceId);
		undirected[edge.sourceId].push_back(edge.destinationId);
		undirected[edge.destinationId].push_back(edge.sourceId);
	}
	for (const QString& id : ids) {
		outgoing[id] = sortedUnique(outgoing.value(id));
		incoming[id] = sortedUnique(incoming.value(id));
		undirected[id] = sortedUnique(undirected.value(id));
	}

	qreal componentY = 0.0;
	QVector<QVector<QString>> components = weakComponents(ids, undirected);
	std::stable_sort(components.begin(), components.end(),
	                 [&](const QVector<QString>& a, const QVector<QString>& b) {
		                 auto previousCenter = [&](const QVector<QString>& component,
		                                           bool* hasPrevious) {
			                 qreal total = 0.0;
			                 int count = 0;
			                 for (const QString& id : component) {
				                 if (nodes[id].hasPreviousPosition) {
					                 total += nodes[id].previousPosition.y();
					                 ++count;
				                 }
			                 }
			                 *hasPrevious = count > 0;
			                 return count > 0 ? total / count : 0.0;
		                 };
		                 bool hasA = false;
		                 bool hasB = false;
		                 const qreal centerA = previousCenter(a, &hasA);
		                 const qreal centerB = previousCenter(b, &hasB);
		                 if (hasA != hasB)
			                 return hasA;
		                 if (hasA && centerA != centerB)
			                 return centerA < centerB;
		                 return a.value(0) < b.value(0);
	                 });
	for (const QVector<QString>& component : components) {
		const LocalLayout local = layoutComponent(component, nodes, outgoing, incoming, options);
		for (const QString& id : component) {
			result.positions[id] = local.positions.value(id) + QPointF(0.0, componentY);
			result.layers[id] = local.layers.value(id);
		}
		componentY += local.size.height() + options.componentSpacing;
	}

	// Pinned nodes retain their exact user position. Unpinned nodes are then
	// moved down only when needed to avoid colliding with pinned/manual cards.
	QVector<QPair<QString, QRectF>> placed;
	for (const QString& id : ids) {
		const RuntimeLayoutNode& node = nodes[id];
		if (!node.pinned || !node.hasPreviousPosition)
			continue;
		result.positions[id] = node.previousPosition;
		placed.push_back({id, QRectF(node.previousPosition,
		                                QSizeF(nodeWidth(node), nodeHeight(node)))});
	}
	for (const QString& id : ids) {
		const RuntimeLayoutNode& node = nodes[id];
		if (node.pinned && node.hasPreviousPosition)
			continue;
		QPointF position = result.positions.value(id);
		QRectF rectangle(position, QSizeF(nodeWidth(node), nodeHeight(node)));
		bool moved = true;
		while (moved) {
			moved = false;
			qreal nextY = rectangle.y();
			for (const auto& occupied : placed) {
				const QRectF expanded = occupied.second.adjusted(
					-options.nodeSpacing * 0.5, -options.nodeSpacing * 0.5,
					 options.nodeSpacing * 0.5,  options.nodeSpacing * 0.5);
				if (rectangle.intersects(expanded)) {
					nextY = qMax(nextY, expanded.bottom() + options.nodeSpacing * 0.5);
					moved = true;
				}
			}
			if (moved) {
				rectangle.moveTop(nextY);
				position.setY(nextY);
			}
		}
		result.positions[id] = position;
		placed.push_back({id, rectangle});
	}

	return result;
}
