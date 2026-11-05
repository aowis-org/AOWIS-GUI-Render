$input a_position, a_texcoord0, a_color0
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>

// x, y = logical viewport size in pixels.
uniform vec4 u_hud_viewport;

// Native HUD (slice 6): vertices in logical pixels, origin top-left, y down
// (MapBgfxHudCanvas). a_color0 is the premultiplied tint.
void main()
{
    vec2 viewport = max(u_hud_viewport.xy, vec2(1.0, 1.0));
    gl_Position = vec4(a_position.x / viewport.x * 2.0 - 1.0,
                       1.0 - a_position.y / viewport.y * 2.0, 0.0, 1.0);
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
}
