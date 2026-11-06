#ifndef MAP_LABEL_SETTINGS_H
#define MAP_LABEL_SETTINGS_H

// Map labels: text placed on the map next to the network entities, like
// EPANET's Map Options -> Notation ("Display Node IDs / Node Values / Link
// IDs / Link Values"). The ID is the entity's EPANET ID (NetworkRenderNode::id,
// NetworkRenderLink::id); the value is the metric the map is currently
// coloured by (the legend's node / link metric, in the legend's unit).
//
// Labels are decluttered: a label is only drawn where it overlaps neither
// another label nor a node or device symbol, so zooming in reveals more of
// them. The bgfx map draws them (map_bgfx_labels.h); MapNavigationWidget
// offers the switches.
struct MapLabelSettings
{
    bool node_ids = false;
    bool link_ids = false;
    // Monitor only: the editor colours nothing by a metric.
    bool node_values = false;
    bool link_values = false;

    bool anyEnabled() const
    {
        return this->node_ids || this->link_ids || this->node_values || this->link_values;
    }
    bool nodesEnabled() const { return this->node_ids || this->node_values; }
    bool linksEnabled() const { return this->link_ids || this->link_values; }

    bool operator==(const MapLabelSettings &other) const
    {
        return this->node_ids == other.node_ids && this->link_ids == other.link_ids
            && this->node_values == other.node_values && this->link_values == other.link_values;
    }
    bool operator!=(const MapLabelSettings &other) const { return !(*this == other); }
};

#endif // MAP_LABEL_SETTINGS_H
