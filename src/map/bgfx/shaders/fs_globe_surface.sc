$input v_texcoord0, v_normal, v_light

#include <bgfx_shader.sh>
#include "globe_hillshade.sh"

SAMPLER2D(s_globe_tile, 0);
// rgb: map background colour, w: background opacity ("slide opacity"), the
// same fade towards the background as the 2D imagery (fs_imagery_2d).
uniform vec4 u_globe_background;

void main()
{
    vec3 tile = texture2D(s_globe_tile, v_texcoord0).rgb;
    // Shaded after the fade, so the relief stays readable on a faded map.
    vec3 faded = mix(tile, u_globe_background.rgb, u_globe_background.w);
    gl_FragColor = vec4(globeHillshade(faded, v_normal, v_light), 1.0);
}
