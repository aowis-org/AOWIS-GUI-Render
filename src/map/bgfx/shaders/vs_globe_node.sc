$input a_position, a_texcoord1, a_color0, a_texcoord2, a_texcoord4
$output v_color0, v_corner

#include <bgfx_shader.sh>
#include "entity_style.sh"
#include "globe_network.sh"

// Generic globe node marker (MapNetworkNodeVertex, six vertices per node):
// a screen-facing disc quad around the projected centre. a_texcoord1: corner
// (-1..1); a_texcoord2: size adjustment (px); a_texcoord4 > 0.5: metre radii
// are measured along the camera's right axis (true perspective size).
// a_color0: the vertex colour, or the entity reference (entity_style.sh):
// the node colour; no marker under a shown icon.
void main()
{
    vec4 color = a_color0;
    if (entityReferenced())
    {
        vec4 texel = entityNodeTexel(a_color0.x);
        color = vec4(entityNodeColor(texel), 1.0);
        if (entityHasFlag(texel, ENTITY_FLAG_HIDDEN) || entityNodeMarkerOff(texel))
            color.a = 0.0;
    }
    vec4 clip_position = mul(u_net_view_proj, vec4(a_position, 1.0));
    vec2 viewport = netViewport();
    float configured_radius = u_net_viewport.w;
    float radius_px = max(configured_radius, 0.0);
    if (configured_radius < 0.0)
    {
        float radius_world = -configured_radius;
        vec3 radius_direction = a_texcoord4 > 0.5 ? u_net_camera_right.xyz : vec3(1.0, 0.0, 0.0);
        vec4 edge_clip = mul(u_net_view_proj, vec4(a_position + radius_direction * radius_world, 1.0));
        radius_px = length((edge_clip.xy / edge_clip.w - clip_position.xy / clip_position.w) * viewport * 0.5);
    }
    vec2 offset_ndc = a_texcoord1 * max(radius_px + a_texcoord2, 0.0) * 2.0 / viewport * step(0.001, color.a);
    clip_position.xy += offset_ndc * clip_position.w;
    gl_Position = clip_position;
    v_color0 = color;
    v_corner = a_texcoord1;
}
