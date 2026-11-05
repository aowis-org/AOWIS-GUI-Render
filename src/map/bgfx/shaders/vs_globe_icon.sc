$input a_position, a_texcoord1, a_texcoord5, a_color0
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>
#include "entity_style.sh"
#include "globe_network.sh"

// Globe pump/valve/tank/reservoir icon (MapNetworkIconVertex, six vertices
// per icon): a screen-aligned quad from the shared icon atlas. Pixel sizes
// are defined at the orbit distance and follow perspective; metre sizes are
// true world sizes. a_color0: the entity reference (entity_style.sh): the
// icon fill of the node or link, the selection colour when selected.
void main()
{
    bool link = a_color0.y > 0.5;
    vec4 texel = entityTexel(a_color0.x, link);
    vec4 color = vec4(entityIconColor(texel, link), 1.0);
    if (u_entity_mode.z > 0.5 && entityHasFlag(texel, ENTITY_FLAG_SELECTED))
        color.rgb = u_entity_colors[ENTITY_COLOR_SELECTION].rgb;
    if (u_entity_options.y < 0.5 || entityHasFlag(texel, ENTITY_FLAG_HIDDEN))
        color.a = 0.0;
    vec4 clip_position = mul(u_net_view_proj, vec4(a_position, 1.0));
    vec2 viewport = netViewport();
    float configured_size = u_net_params.y;
    float depth = max(abs(clip_position.w), 0.000001);
    float icon_size_px = max(configured_size, 0.0);
    if (configured_size < 0.0)
        icon_size_px = -configured_size * viewport.y / (2.0 * depth * NET_TAN_HALF_FOV);
    else
        icon_size_px *= max(u_net_params.x, 0.000001) / depth;
    vec2 offset_ndc = a_texcoord1 * icon_size_px * 2.0 / viewport * step(0.001, color.a);
    clip_position.xy += offset_ndc * clip_position.w;
    gl_Position = clip_position;
    v_texcoord0 = a_texcoord5;
    v_color0 = color;
}
