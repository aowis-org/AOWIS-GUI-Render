#ifndef MAP_RETAINED_NETWORK_SCENE_H
#define MAP_RETAINED_NETWORK_SCENE_H

#include "network/network_render_snapshot.h"

#include <QHash>
#include <QVector>

#include <cmath>

// Backend-neutral ownership and lookup for a prepared network snapshot.
// Geometry, UUID identity and spatial grouping depend only on geometry_revision.
// visual_revision is bookkeeping for non-geometric changes; a camera change
// never enters this interface. Actual vertex/GPU chunk updates come later.
struct MapRetainedNetworkUpdate
{
    bool geometry_changed = false;
    bool visual_changed = false;
};

struct MapRetainedNetworkEntity
{
    InfrastructureEntity type = InfrastructureEntity::Unknown;
    quint32 render_id = 0;
    qsizetype snapshot_index = -1;
};

// A *coarse anchor index*, not a visibility/culling result. In particular,
// long links can cross multiple cells; later spatial culling must use complete
// bounds rather than treating this anchor as a coverage guarantee.
struct MapRetainedNetworkChunk
{
    QVector<qsizetype> node_indices;
    QVector<qsizetype> link_indices;
    QVector<qsizetype> demand_attachment_indices;
};

class MapRetainedNetworkScene
{
public:
    MapRetainedNetworkUpdate setSnapshot(const NetworkRenderSnapshot &snapshot)
    {
        MapRetainedNetworkUpdate update;
        if (!this->snapshot_initialized
            || this->network_snapshot.geometry_revision != snapshot.geometry_revision)
        {
            update.geometry_changed = true;
            update.visual_changed = true;
            this->network_snapshot = snapshot;
            this->snapshot_initialized = true;
            rebuildIndex();
        }
        else if (this->network_snapshot.visual_revision != snapshot.visual_revision)
        {
            // NetworkRenderSnapshot's visual revision carries no new geometry.
            // Replacing the snapshot would copy every demand point needlessly.
            // IDs can change without geometry (map labels show them): take
            // them over (same geometry revision = same entities, same order;
            // QString copies are implicitly shared).
            this->network_snapshot.visual_revision = snapshot.visual_revision;
            update.visual_changed = true;
            if (this->network_snapshot.nodes.size() == snapshot.nodes.size()
                && this->network_snapshot.links.size() == snapshot.links.size())
            {
                for (qsizetype index = 0; index < snapshot.nodes.size(); ++index)
                    this->network_snapshot.nodes[index].id = snapshot.nodes.at(index).id;
                for (qsizetype index = 0; index < snapshot.links.size(); ++index)
                    this->network_snapshot.links[index].id = snapshot.links.at(index).id;
            }
        }
        return update;
    }

    bool hasSnapshot() const { return this->snapshot_initialized; }
    const NetworkRenderSnapshot &snapshot() const { return this->network_snapshot; }
    const QHash<QUuid, MapRetainedNetworkEntity> &nodesByUuid() const
    {
        return this->nodes_by_uuid;
    }
    const QHash<QUuid, MapRetainedNetworkEntity> &linksByUuid() const
    {
        return this->links_by_uuid;
    }
    const QHash<QUuid, qsizetype> &attachmentsByDemandPointUuid() const
    {
        return this->attachments_by_uuid;
    }
    const QHash<quint64, MapRetainedNetworkChunk> &chunks() const
    {
        return this->spatial_chunks;
    }

    // Geographical anchor cells of 1/4 degree, completely independent of
    // viewport zoom and terrain/imagery LOD. Coordinates are normalized so
    // antimeridian-equivalent longitudes yield the same anchor cell.
    static bool chunkKey(const CoordinateWGS84 &coordinate, quint64 *key)
    {
        if (key == nullptr || !std::isfinite(coordinate.longitude_deg)
            || !std::isfinite(coordinate.latitude_deg)
            || coordinate.latitude_deg < -90.0 || coordinate.latitude_deg > 90.0)
        {
            return false;
        }
        double lon = std::fmod(coordinate.longitude_deg + 180.0, 360.0);
        if (lon < 0.0)
            lon += 360.0;
        const quint32 x = quint32(std::floor(lon * 4.0));
        const quint32 y = quint32(std::floor((coordinate.latitude_deg + 90.0) * 4.0));
        *key = (quint64(y < 720 ? y : 719) << 32) | quint64(x < 1440 ? x : 0);
        return true;
    }

private:
    void rebuildIndex()
    {
        this->nodes_by_uuid.clear();
        this->links_by_uuid.clear();
        this->attachments_by_uuid.clear();
        this->spatial_chunks.clear();
        this->nodes_by_uuid.reserve(this->network_snapshot.nodes.size());
        this->links_by_uuid.reserve(this->network_snapshot.links.size());
        this->attachments_by_uuid.reserve(
            this->network_snapshot.demand_point_attachments.size());

        for (qsizetype index = 0; index < this->network_snapshot.nodes.size(); ++index)
        {
            const NetworkRenderNode &node = this->network_snapshot.nodes.at(index);
            this->nodes_by_uuid.insert(node.uuid,
                MapRetainedNetworkEntity{node.entity_type, node.render_id, index});
            quint64 key = 0;
            if (chunkKey(node.coordinate_wgs84, &key))
                this->spatial_chunks[key].node_indices.append(index);
        }
        for (qsizetype index = 0; index < this->network_snapshot.links.size(); ++index)
        {
            const NetworkRenderLink &link = this->network_snapshot.links.at(index);
            this->links_by_uuid.insert(link.uuid,
                MapRetainedNetworkEntity{link.entity_type, link.render_id, index});
            for (const CoordinateWGS84 &vertex : link.vertices_wgs84)
            {
                quint64 key = 0;
                if (chunkKey(vertex, &key))
                {
                    this->spatial_chunks[key].link_indices.append(index);
                    break;
                }
            }
        }
        for (qsizetype index = 0;
             index < this->network_snapshot.demand_point_attachments.size(); ++index)
        {
            const NetworkRenderDemandPointAttachment &attachment =
                this->network_snapshot.demand_point_attachments.at(index);
            this->attachments_by_uuid.insert(attachment.demand_point_uuid, index);
            quint64 key = 0;
            if (chunkKey(attachment.demand_point_coordinate_wgs84, &key))
                this->spatial_chunks[key].demand_attachment_indices.append(index);
        }
    }

    NetworkRenderSnapshot network_snapshot;
    QHash<QUuid, MapRetainedNetworkEntity> nodes_by_uuid;
    QHash<QUuid, MapRetainedNetworkEntity> links_by_uuid;
    QHash<QUuid, qsizetype> attachments_by_uuid;
    QHash<quint64, MapRetainedNetworkChunk> spatial_chunks;
    bool snapshot_initialized = false;
};

#endif // MAP_RETAINED_NETWORK_SCENE_H
