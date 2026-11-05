$input a_position, a_texcoord0, a_texcoord1, a_texcoord2
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>
#include "entity_style.sh"

// Chunk-local world -> clip: xy = chunk origin in clip, zw = clip per world unit.
uniform vec4 u_network_camera;
// xy = clip per screen pixel (x right, y down).
uniform vec4 u_network_pixel;
// x = icon size in pixels (symbology icon size, px or metres converted per frame).
uniform vec4 u_icon_style;

// Icons: a_texcoord0 = corner offset in units of the icon size (the atlas
// aspect ratio is baked in), a_texcoord1 = atlas coordinates, a_texcoord2 =
// (render id, 1 for a link icon / 0 for a node icon). The fill colour comes
// from the entity's data (entity_style.sh); hidden icons collapse.
void main()
{
    bool link = a_texcoord2.y > 0.5;
    vec4 texel = entityTexel(a_texcoord2.x, link);
    vec4 color = vec4(entityIconColor(texel, link), 1.0);
    if (u_entity_options.y < 0.5 || entityHasFlag(texel, ENTITY_FLAG_HIDDEN))
        color.a = 0.0;

    vec2 clip = u_network_camera.xy + a_position.xy * u_network_camera.zw;
    vec2 offset = a_texcoord0.xy * u_icon_style.x * u_network_pixel.xy * step(0.001, color.a);
    gl_Position = vec4(clip + offset, 0.0, 1.0);
    v_color0 = color;
    v_texcoord0 = a_texcoord1;
}
