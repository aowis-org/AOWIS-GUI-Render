$input a_position, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>
#include "entity_style.sh"

// Chunk-local world -> clip: xy = chunk origin in clip, zw = clip per world unit.
uniform vec4 u_network_camera;
// xy = clip per screen pixel (x right, y down).
uniform vec4 u_network_pixel;
// x = chevron length px, y = half stroke px,
// z = threshold level (MapBgfx2dFlow::thresholdLevel), w = world pixels.
uniform vec4 u_flow_style;
// x = minimum link length on screen, px.
uniform vec4 u_flow_limits;

// Zoom-independent flow chevrons (see map_bgfx_2d_mesh.h). The geometry is
// static; visibility is computed here from the continuous zoom:
//   a_texcoord2.x = hierarchy level, a_texcoord2.y = link length (world),
//   a_texcoord3.x = the link's render id: its current flow direction and
//   colour come from the link's data (entity_style.sh), so a time step
//   turns or hides chevrons without touching them.
// Levels at or above the threshold are shown, the level just below fades in
// over one octave, so chevrons never pop. Hidden chevrons collapse to a
// zero-area quad and produce no fragments.
void main()
{
    vec4 texel = entityLinkTexel(a_texcoord3.x);
    float flow_sign = texel.y;
    float shown = u_entity_options.z * (1.0 - entityFlag(texel.z, ENTITY_FLAG_HIDDEN));
    vec2 clip = u_network_camera.xy + a_position.xy * u_network_camera.zw;
    float level_visibility = clamp(a_texcoord2.x - u_flow_style.z + 1.0, 0.0, 1.0);
    float long_enough = step(u_flow_limits.x, a_texcoord2.y * u_flow_style.w);
    float visibility = level_visibility * long_enough * abs(flow_sign) * shown;

    vec2 direction = a_texcoord0.xy * flow_sign;
    vec2 normal = vec2(-direction.y, direction.x);
    float half_extent = 0.5 * u_flow_style.x + u_flow_style.y + 1.0;
    // Quad-local pixel coordinates: x along the flow, y across it.
    vec2 local = a_texcoord1.xy * half_extent;
    vec2 offset = (direction * local.x + normal * local.y) * visibility;
    gl_Position = vec4(clip + offset * u_network_pixel.xy, 0.0, 1.0);
    v_color0 = vec4(entityFlowColor(entityLinkColor(texel)), visibility);
    v_texcoord0 = local;
}
