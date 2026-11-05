$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

// x = marker half size in pixels.
uniform vec4 u_marker_style;

// Round node marker with a one-pixel antialiased rim.
void main()
{
    float radius = length(v_texcoord0.xy);
    float rim = 1.0 / max(u_marker_style.x, 1.0);
    float coverage = 1.0 - smoothstep(1.0 - rim, 1.0, radius);
    if (coverage <= 0.0)
        discard;
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * coverage);
}
