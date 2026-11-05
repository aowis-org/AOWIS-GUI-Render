#ifndef MAP_BGFX_GLOBE_ICON_INDEX_H
#define MAP_BGFX_GLOBE_ICON_INDEX_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

// CPU-only, retained ECEF spatial index for 3D Editor icon anchors.
// Its points are at ellipsoid height zero. The frustum's relief allowance
// expands every plane far enough to include terrain-draped positions; false
// positives are fine, but false negatives would remove visible icons.
// Geometry changes rebuild the index; camera movement only queries it.
class MapBgfxGlobeIconIndex
{
public:
    struct Position
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    struct Plane
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double offset = 0.0;
    };

    struct Frustum
    {
        // Four homogeneous clip-space side planes; coordinates are absolute
        // ECEF metres. The side planes already include the icon-edge margin.
        std::array<Plane, 4> sides;
        // Safe maximum absolute terrain displacement in metres, including
        // vertical exaggeration and negative elevation.
        double relief_allowance_m = 0.0;
        bool valid = false;
    };

    struct QueryStats
    {
        std::size_t visited_nodes = 0;
        std::size_t examined_points = 0;
        std::size_t returned_points = 0;
    };

    void rebuild(const std::vector<Position> &positions)
    {
        this->points = positions;
        this->ordering.resize(positions.size());
        std::iota(this->ordering.begin(), this->ordering.end(), std::size_t(0));
        this->nodes.clear();
        this->nodes.reserve(positions.size() / LeafSize * 2 + 2);
        if (!positions.empty())
            build(0, positions.size());
        ++this->build_count;
    }

    // Returns original input indices, in input order. With no valid camera
    // frustum (e.g. stand-alone icon rendering tests), returns all anchors.
    void query(const Frustum &frustum, std::vector<std::size_t> *candidates,
               QueryStats *stats = nullptr) const
    {
        queryUnordered(frustum, candidates, stats);
        if (candidates != nullptr)
            std::sort(candidates->begin(), candidates->end());
    }

    // The 3D renderer sorts visible sprites by camera distance afterwards,
    // so sorting all frustum candidates by snapshot index is wasted work.
    // Callers requiring stable source order must use query() instead.
    void queryUnordered(const Frustum &frustum, std::vector<std::size_t> *candidates,
                        QueryStats *stats = nullptr) const
    {
        if (candidates == nullptr)
            return;
        candidates->clear();
        QueryStats measured;
        if (!this->nodes.empty())
        {
            // Dense, fully included views do not benefit from traversing
            // every kd-tree leaf. Copy the original index range directly;
            // the exact screen-space and horizon tests remain authoritative.
            const Node &root = this->nodes.front();
            if (fullyIncluded(root.minimum, root.maximum, frustum))
            {
                candidates->resize(this->points.size());
                std::iota(candidates->begin(), candidates->end(), std::size_t(0));
                measured.visited_nodes = 1;
            }
            else
                visit(0, frustum, candidates, &measured);
        }
        measured.returned_points = candidates->size();
        if (stats != nullptr)
            *stats = measured;
    }

    std::size_t size() const { return this->points.size(); }
    std::size_t buildCount() const { return this->build_count; }
    // Capacity-based estimate for benchmark comparisons, excluding allocator
    // metadata and the caller-owned source geometry.
    std::size_t estimatedStorageBytes() const
    {
        return this->points.capacity() * sizeof(Position)
            + this->ordering.capacity() * sizeof(std::size_t)
            + this->nodes.capacity() * sizeof(Node);
    }

private:
    static constexpr std::size_t LeafSize = 24;
    struct Node
    {
        Position minimum;
        Position maximum;
        std::size_t first = 0;
        std::size_t last = 0;
        int left = -1;
        int right = -1;
    };

    static double axis(const Position &point, int index)
    {
        return index == 0 ? point.x : (index == 1 ? point.y : point.z);
    }

    static void include(Position *minimum, Position *maximum, const Position &point)
    {
        minimum->x = std::min(minimum->x, point.x);
        minimum->y = std::min(minimum->y, point.y);
        minimum->z = std::min(minimum->z, point.z);
        maximum->x = std::max(maximum->x, point.x);
        maximum->y = std::max(maximum->y, point.y);
        maximum->z = std::max(maximum->z, point.z);
    }

    int build(std::size_t first, std::size_t last)
    {
        Node node;
        node.first = first;
        node.last = last;
        node.minimum = node.maximum = this->points[this->ordering[first]];
        for (std::size_t index = first + 1; index < last; ++index)
            include(&node.minimum, &node.maximum, this->points[this->ordering[index]]);
        const int result = int(this->nodes.size());
        this->nodes.push_back(node);
        if (last - first <= LeafSize)
            return result;

        const double dx = node.maximum.x - node.minimum.x;
        const double dy = node.maximum.y - node.minimum.y;
        const double dz = node.maximum.z - node.minimum.z;
        const int split_axis = dx >= dy && dx >= dz ? 0 : (dy >= dz ? 1 : 2);
        const std::size_t middle = first + (last - first) / 2;
        std::nth_element(this->ordering.begin() + first, this->ordering.begin() + middle,
                         this->ordering.begin() + last, [this, split_axis](std::size_t a, std::size_t b)
        {
            const double left = axis(this->points[a], split_axis);
            const double right = axis(this->points[b], split_axis);
            return left != right ? left < right : a < b;
        });
        const int left = build(first, middle);
        const int right = build(middle, last);
        this->nodes[result].left = left;
        this->nodes[result].right = right;
        return result;
    }

    static bool intersects(const Position &minimum, const Position &maximum,
                           const Frustum &frustum)
    {
        if (!frustum.valid)
            return true;
        for (const Plane &plane : frustum.sides)
        {
            // Largest signed distance in the box. If that entire box lies
            // outside a plane even after allowing for terrain, prune it.
            const double farthest = plane.x * (plane.x >= 0.0 ? maximum.x : minimum.x)
                + plane.y * (plane.y >= 0.0 ? maximum.y : minimum.y)
                + plane.z * (plane.z >= 0.0 ? maximum.z : minimum.z) + plane.offset;
            const double normal_length = std::sqrt(plane.x * plane.x + plane.y * plane.y
                                                   + plane.z * plane.z);
            if (farthest < -frustum.relief_allowance_m * normal_length)
                return false;
        }
        return true;
    }

    static bool fullyIncluded(const Position &minimum, const Position &maximum,
                              const Frustum &frustum)
    {
        if (!frustum.valid)
            return true;
        for (const Plane &plane : frustum.sides)
        {
            // Smallest signed distance of any point in the node's box.
            const double nearest = plane.x * (plane.x >= 0.0 ? minimum.x : maximum.x)
                + plane.y * (plane.y >= 0.0 ? minimum.y : maximum.y)
                + plane.z * (plane.z >= 0.0 ? minimum.z : maximum.z) + plane.offset;
            const double normal_length = std::sqrt(plane.x * plane.x + plane.y * plane.y
                                                   + plane.z * plane.z);
            if (nearest < -frustum.relief_allowance_m * normal_length)
                return false;
        }
        return true;
    }

    void visit(int node_index, const Frustum &frustum,
               std::vector<std::size_t> *candidates, QueryStats *stats) const
    {
        const Node &node = this->nodes[std::size_t(node_index)];
        ++stats->visited_nodes;
        if (!intersects(node.minimum, node.maximum, frustum))
            return;
        if (node.left >= 0)
        {
            visit(node.left, frustum, candidates, stats);
            visit(node.right, frustum, candidates, stats);
            return;
        }
        for (std::size_t index = node.first; index < node.last; ++index)
        {
            const std::size_t entry = this->ordering[index];
            ++stats->examined_points;
            const Position &point = this->points[entry];
            if (intersects(point, point, frustum))
                candidates->push_back(entry);
        }
    }

    std::vector<Position> points;
    std::vector<std::size_t> ordering;
    std::vector<Node> nodes;
    std::size_t build_count = 0;
};

#endif // MAP_BGFX_GLOBE_ICON_INDEX_H
