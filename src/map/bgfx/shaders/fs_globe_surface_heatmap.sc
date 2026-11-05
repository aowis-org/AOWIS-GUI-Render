$input v_texcoord0, v_texcoord1, v_normal, v_light

#include <bgfx_shader.sh>
#include "globe_hillshade.sh"

SAMPLER2D(s_globe_tile, 0);
SAMPLER2D(s_globe_heatmap, 1);
uniform vec4 u_globe_background;
// x: heatmap opacity (0..1).
uniform vec4 u_globe_heatmap_style;

// fs_globe_surface plus the premultiplied heatmap on top, as the QRhi
// globe's array shader: rgb * (1 - a * opacity) + heat.rgb * opacity. The
// relief shades the map only; the heat colours stay unshaded (readable).
void main()
{
    vec3 tile = texture2D(s_globe_tile, v_texcoord0).rgb;
    vec3 mixed_rgb = globeHillshade(mix(tile, u_globe_background.rgb, u_globe_background.w), v_normal, v_light);
    vec4 heat = texture2D(s_globe_heatmap, v_texcoord1);
    float opacity = clamp(u_globe_heatmap_style.x, 0.0, 1.0);
    mixed_rgb = mixed_rgb * (1.0 - heat.a * opacity) + heat.rgb * opacity;
    gl_FragColor = vec4(mixed_rgb, 1.0);
}
