$input a_position, a_texcoord0, a_texcoord1
$output v_color0

#include <bgfx_shader.sh>
#include "entity_style.sh"

// Chunk-local world -> clip: xy = chunk origin in clip, zw = clip per world unit.
uniform vec4 u_network_camera;
// xy = clip per screen pixel (x right, y down).
uniform vec4 u_network_pixel;
// x = half line width in pixels, y = cap extension in pixels.
uniform vec4 u_line_style;

// Thick line quad extrusion (see map_bgfx_2d_mesh.h). a_position is this
// endpoint, a_texcoord0 the opposite endpoint, a_texcoord1.x the side
// (+1/-1), a_texcoord1.y the render id of the link (or of the demand point
// of an attachment line: u_entity_mode.y says which). World and screen are
// both y-down and isotropically scaled, so the world direction is the screen
// direction; offsets are in pixels, i.e. widths stay constant on screen at
// every zoom. The colour comes from the entity's data (entity_style.sh);
// an invisible segment collapses to a zero-area quad.
void main()
{
    bool link = u_entity_mode.y > 0.5;
    vec4 texel = entityTexel(a_texcoord1.y, link);
    vec4 color = vec4(0.0, 0.0, 0.0, 0.0);
    if (entityMode(ENTITY_MODE_SELECTION))
        color = vec4(u_entity_colors[ENTITY_COLOR_SELECTION].rgb, 1.0);
    else if (entityMode(ENTITY_MODE_DIAGNOSTIC))
        color = entityDiagnosticColor(texel);
    else if (!entityHasFlag(texel, ENTITY_FLAG_HIDDEN))
        color = vec4(link ? entityLinkColor(texel) : entityNodeColor(texel), 1.0);

    vec2 clip = u_network_camera.xy + a_position.xy * u_network_camera.zw;
    vec2 delta = a_texcoord0.xy - a_position.xy;
    float len = length(delta);
    vec2 dir = vec2(1.0, 0.0);
    if (len > 0.0)
        dir = delta / len;
    vec2 normal = vec2(-dir.y, dir.x);
    vec2 offset = normal * (a_texcoord1.x * u_line_style.x) - dir * u_line_style.y;
    gl_Position = vec4(clip + offset * u_network_pixel.xy * step(0.001, color.a), 0.0, 1.0);
    v_color0 = color;
}
