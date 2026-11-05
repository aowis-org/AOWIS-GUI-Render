#include "map/render/map_node_declutter.h"

#include <QPair>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace
{
struct DeclutterNode
{
    quint32 render_id = 0;
    QPointF world_center;
};

qint64 declutterCellCoordinate(double value, double cell_size)
{
    return qint64(std::floor(value / cell_size));
}

// Union-find so that nodes which are each close to a shared neighbour end up
// in the same cluster even when they are not directly within range of one
// another - e.g. three coincident nodes sharing a single point.
class DeclutterDisjointSet
{
public:
    explicit DeclutterDisjointSet(int size)
    {
        this->parent_by_index.resize(size);
        for (int index = 0; index < size; ++index)
            this->parent_by_index[index] = index;
    }

    int find(int index)
    {
        while (this->parent_by_index.at(index) != index)
        {
            this->parent_by_index[index] =
                this->parent_by_index.at(this->parent_by_index.at(index));
            index = this->parent_by_index.at(index);
        }
        return index;
    }

    void unite(int first, int second)
    {
        const int first_root = find(first);
        const int second_root = find(second);
        if (first_root != second_root)
            this->parent_by_index[first_root] = second_root;
    }

private:
    QVector<int> parent_by_index;
};
}

QHash<quint32, QPointF> computeNodeDeclutterOffsets(
    const QVector<MapNodeDeclutterInput> &nodes,
    double minimum_separation_world)
{
    QHash<quint32, QPointF> offsets;
    if (nodes.size() < 2 || !(minimum_separation_world > 0.0))
        return offsets;

    QVector<DeclutterNode> ordered_nodes;
    ordered_nodes.reserve(nodes.size());
    for (const MapNodeDeclutterInput &input : nodes)
        ordered_nodes.append({input.render_id, input.world_center});

    // Sort by render_id so clustering and, further down, angle assignment
    // are fully deterministic no matter what order the caller supplies.
    std::sort(ordered_nodes.begin(), ordered_nodes.end(),
        [](const DeclutterNode &first, const DeclutterNode &second)
    {
        return first.render_id < second.render_id;
    });

    // Bucket nodes into a spatial hash keyed by cell so only nearby
    // candidates are compared, instead of every pair, which keeps this cheap
    // for large networks.
    QHash<QPair<qint64, qint64>, QVector<int>> nodes_by_cell;
    for (int index = 0; index < ordered_nodes.size(); ++index)
    {
        const QPointF &center = ordered_nodes.at(index).world_center;
        const QPair<qint64, qint64> cell(
            declutterCellCoordinate(center.x(), minimum_separation_world),
            declutterCellCoordinate(center.y(), minimum_separation_world));
        nodes_by_cell[cell].append(index);
    }

    DeclutterDisjointSet clusters(ordered_nodes.size());
    for (int index = 0; index < ordered_nodes.size(); ++index)
    {
        const QPointF &center = ordered_nodes.at(index).world_center;
        const qint64 cell_x = declutterCellCoordinate(center.x(), minimum_separation_world);
        const qint64 cell_y = declutterCellCoordinate(center.y(), minimum_separation_world);

        for (qint64 neighbor_dx = -1; neighbor_dx <= 1; ++neighbor_dx)
        {
            for (qint64 neighbor_dy = -1; neighbor_dy <= 1; ++neighbor_dy)
            {
                const QPair<qint64, qint64> neighbor_cell(
                    cell_x + neighbor_dx, cell_y + neighbor_dy);
                const QHash<QPair<qint64, qint64>, QVector<int>>::const_iterator bucket =
                    nodes_by_cell.constFind(neighbor_cell);
                if (bucket == nodes_by_cell.constEnd())
                    continue;

                for (int other_index : bucket.value())
                {
                    // Compare each unordered pair once.
                    if (other_index <= index)
                        continue;

                    const QPointF delta =
                        ordered_nodes.at(other_index).world_center - center;
                    const double distance = std::hypot(delta.x(), delta.y());
                    if (distance < minimum_separation_world)
                        clusters.unite(index, other_index);
                }
            }
        }
    }

    QHash<int, QVector<int>> member_indices_by_cluster_root;
    for (int index = 0; index < ordered_nodes.size(); ++index)
        member_indices_by_cluster_root[clusters.find(index)].append(index);

    for (QHash<int, QVector<int>>::const_iterator cluster_iterator =
             member_indices_by_cluster_root.constBegin();
         cluster_iterator != member_indices_by_cluster_root.constEnd();
         ++cluster_iterator)
    {
        const QVector<int> &member_indices = cluster_iterator.value();
        if (member_indices.size() < 2)
            continue;

        QPointF centroid(0.0, 0.0);
        for (int member_index : member_indices)
            centroid += ordered_nodes.at(member_index).world_center;
        centroid /= double(member_indices.size());

        // Evenly spaced points on a circle whose chord length between
        // neighbouring points equals minimum_separation_world, so every
        // member of the cluster ends up at least that far from every other.
        const double spread_radius = minimum_separation_world
            / (2.0 * std::sin(M_PI / double(member_indices.size())));

        for (int member_position = 0; member_position < member_indices.size();
             ++member_position)
        {
            const int member_index = member_indices.at(member_position);
            const double angle =
                2.0 * M_PI * double(member_position) / double(member_indices.size());
            const QPointF spread_position(
                centroid.x() + spread_radius * std::cos(angle),
                centroid.y() + spread_radius * std::sin(angle));
            offsets.insert(
                ordered_nodes.at(member_index).render_id,
                spread_position - ordered_nodes.at(member_index).world_center);
        }
    }

    return offsets;
}
