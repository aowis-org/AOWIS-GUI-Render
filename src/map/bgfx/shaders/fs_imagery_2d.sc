$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_tile, 0);
// rgb: map background colour, w: background opacity ("slide opacity") that
// fades the imagery towards the background under the network.
uniform vec4 u_imagery_background;

void main()
{
    vec3 tile = texture2D(s_tile, v_texcoord0).rgb;
    gl_FragColor = vec4(mix(tile, u_imagery_background.rgb, u_imagery_background.w), 1.0);
}
