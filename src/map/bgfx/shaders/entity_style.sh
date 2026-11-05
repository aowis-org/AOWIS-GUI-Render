// GPU network colouring shared by the 2D and globe network vertex shaders
// (MapBgfxEntityStyle, map_bgfx_entity_style.h). Geometry references an
// entity by its render id; colours come from per-entity data textures and the
// palette texture, so a time step, metric, palette or range change never
// touches a vertex buffer.

// Per-entity data (RGBA32F, ENTITY_TEXTURE_WIDTH texels per row, texel =
// render id). x: colouring value; y: nodes: heatmap value, links: flow
// direction (-1, 0, +1); z: flag bits (ENTITY_FLAG_*).
SAMPLER2D(s_entity_nodes, 1);
SAMPLER2D(s_entity_links, 2);
// 256 x 3 RGBA8 ramps: row 0 nodes, 1 links, 2 heatmap.
SAMPLER2D(s_entity_palette, 3);

// Metric ranges: x minimum, y 1 / (maximum - minimum), z base fraction
// (0.5 for an empty range), w state: 0 visual None, 1 on the ramp, 2 no
// finite range (every value unavailable).
uniform vec4 u_entity_node;
uniform vec4 u_entity_link;
uniform vec4 u_entity_heat;
// x show junctions, y show icons, z show flow arrows.
uniform vec4 u_entity_options;
// Fixed colours (ENTITY_COLOR_*).
uniform vec4 u_entity_colors[9];
// x mode (ENTITY_MODE_*), y table of the draw's geometry where the vertex
// does not carry it (0 nodes, 1 links), z: selected icons take the
// selection colour (globe).
uniform vec4 u_entity_mode;

#define ENTITY_TEXTURE_WIDTH 2048.0

#define ENTITY_MODE_VERTEX_COLOR 0.0
#define ENTITY_MODE_SYMBOLOGY 1.0
#define ENTITY_MODE_SELECTION 2.0
#define ENTITY_MODE_DIAGNOSTIC 3.0
#define ENTITY_MODE_HEATMAP 4.0

#define ENTITY_FLAG_HAS_VALUE 1.0
#define ENTITY_FLAG_HAS_HEAT 2.0
#define ENTITY_FLAG_HIDDEN 4.0
#define ENTITY_FLAG_SELECTED 8.0
#define ENTITY_FLAG_ERROR 16.0
#define ENTITY_FLAG_STALE 32.0
#define ENTITY_FLAG_KIND 64.0
#define ENTITY_FLAG_HAS_ICON 128.0

#define ENTITY_COLOR_NODE_BASE 0
#define ENTITY_COLOR_PIPE_BASE 1
#define ENTITY_COLOR_DEVICE_BASE 2
#define ENTITY_COLOR_UNAVAILABLE 3
#define ENTITY_COLOR_ICON_FILL 4
#define ENTITY_COLOR_ICON_UNAVAILABLE 5
#define ENTITY_COLOR_SELECTION 6
#define ENTITY_COLOR_ERROR 7
#define ENTITY_COLOR_STALE 8

bool entityMode(float mode)
{
    return abs(u_entity_mode.x - mode) < 0.5;
}

// 1.0 when the flag bit is set (flags is an exact integer in a float).
float entityFlag(float flags, float bit)
{
    return mod(floor(flags / bit), 2.0);
}

bool entityHasFlag(vec4 texel, float bit)
{
    return entityFlag(texel.z, bit) > 0.5;
}

ivec2 entityTexelCoord(float render_id)
{
    float row = floor(render_id / ENTITY_TEXTURE_WIDTH);
    return ivec2(int(render_id - row * ENTITY_TEXTURE_WIDTH), int(row));
}

vec4 entityNodeTexel(float render_id)
{
    return texelFetch(s_entity_nodes, entityTexelCoord(render_id), 0);
}

vec4 entityLinkTexel(float render_id)
{
    return texelFetch(s_entity_links, entityTexelCoord(render_id), 0);
}

vec4 entityTexel(float render_id, bool link)
{
    return link ? entityLinkTexel(render_id) : entityNodeTexel(render_id);
}

// The CPU's bucket of a fraction (networkSymbologyRampColor()).
vec3 entityRampColor(float fraction, float row)
{
    float bucket = floor(clamp(fraction, 0.0, 1.0) * 255.0 + 0.5);
    return texelFetch(s_entity_palette, ivec2(int(bucket), int(row)), 0).rgb;
}

// Continuous ramp (networkSymbologyInterpolatedRampColor(), the heatmap).
vec3 entityRampColorSmooth(float fraction, float row)
{
    float position = clamp(fraction, 0.0, 1.0) * 255.0;
    float lower = floor(position);
    float upper = min(lower + 1.0, 255.0);
    vec3 lower_color = texelFetch(s_entity_palette, ivec2(int(lower), int(row)), 0).rgb;
    vec3 upper_color = texelFetch(s_entity_palette, ivec2(int(upper), int(row)), 0).rgb;
    return mix(lower_color, upper_color, position - lower);
}

float entityFraction(vec4 range, float value)
{
    return range.z + (value - range.x) * range.y;
}

// networkSymbologyColor(): the base colour while the visual is None, the
// unavailable colour without a value or range, else the ramp bucket.
vec3 entityMetricColor(vec4 texel, vec4 range, float row, vec3 base_color)
{
    if (range.w < 0.5)
        return base_color;
    if (range.w > 1.5 || !entityHasFlag(texel, ENTITY_FLAG_HAS_VALUE))
        return u_entity_colors[ENTITY_COLOR_UNAVAILABLE].rgb;
    return entityRampColor(entityFraction(range, texel.x), row);
}

vec3 entityNodeColor(vec4 texel)
{
    return entityMetricColor(texel, u_entity_node, 0.0, u_entity_colors[ENTITY_COLOR_NODE_BASE].rgb);
}

vec3 entityLinkColor(vec4 texel)
{
    vec3 base_color = entityHasFlag(texel, ENTITY_FLAG_KIND) ? u_entity_colors[ENTITY_COLOR_DEVICE_BASE].rgb
                                                              : u_entity_colors[ENTITY_COLOR_PIPE_BASE].rgb;
    return entityMetricColor(texel, u_entity_link, 1.0, base_color);
}

// Icon fill: the default fill while not colourised, the "unavailable" fill
// without a value, else the metric colour.
vec3 entityIconColor(vec4 texel, bool link)
{
    vec4 range = link ? u_entity_link : u_entity_node;
    if (range.w < 0.5)
        return u_entity_colors[ENTITY_COLOR_ICON_FILL].rgb;
    if (range.w > 1.5 || !entityHasFlag(texel, ENTITY_FLAG_HAS_VALUE))
        return u_entity_colors[ENTITY_COLOR_ICON_UNAVAILABLE].rgb;
    return entityRampColor(entityFraction(range, texel.x), link ? 1.0 : 0.0);
}

// Flow chevrons contrast with their link (QRhi flowDirectionColor()).
vec3 entityFlowColor(vec3 link_color)
{
    float luminance = dot(link_color, vec3(0.2126, 0.7152, 0.0722));
    return luminance >= 150.0 / 255.0 ? vec3(0.0, 0.0, 0.0) : vec3(1.0, 1.0, 1.0);
}

// Heatmap spot colour; alpha 0: no spot (no value or no finite range).
vec4 entityHeatColor(vec4 node_texel)
{
    if (abs(u_entity_heat.w - 1.0) > 0.5 || !entityHasFlag(node_texel, ENTITY_FLAG_HAS_HEAT))
        return vec4(0.0, 0.0, 0.0, 0.0);
    return vec4(entityRampColorSmooth(entityFraction(u_entity_heat, node_texel.y), 2.0), 1.0);
}

// Simulation diagnostic colour; alpha 0 for entities without one.
vec4 entityDiagnosticColor(vec4 texel)
{
    if (!entityHasFlag(texel, ENTITY_FLAG_ERROR))
        return vec4(0.0, 0.0, 0.0, 0.0);
    return entityHasFlag(texel, ENTITY_FLAG_STALE) ? vec4(u_entity_colors[ENTITY_COLOR_STALE].rgb, 1.0)
                                                   : vec4(u_entity_colors[ENTITY_COLOR_ERROR].rgb, 1.0);
}

// A node whose marker is switched off: a junction while junctions are
// hidden, an entity with an icon while icons are shown.
bool entityNodeMarkerOff(vec4 texel)
{
    bool junction = entityHasFlag(texel, ENTITY_FLAG_KIND);
    return (junction && u_entity_options.x < 0.5)
        || (u_entity_options.y > 0.5 && entityHasFlag(texel, ENTITY_FLAG_HAS_ICON));
}

// Entity-coloured globe geometry (the network's base streams) carries no
// colour: its colour fields hold the entity reference
// (MapNetworkEntityReference, map_network_render_data.h): x render id,
// y 1 for a link / 0 for a node, z flow chevron orientation (+1 / -1). The
// colour comes from the entity's data in the symbology mode; the highlight
// streams keep real vertex colours (vertex colour mode).
bool entityReferenced()
{
    return entityMode(ENTITY_MODE_SYMBOLOGY);
}
