$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hud_atlas, 0);

// Premultiplied atlas texel (white text runs, images, the white texel for
// solid shapes) times the premultiplied vertex tint; blended One,
// OneMinusSrcAlpha.
void main()
{
    gl_FragColor = texture2D(s_hud_atlas, v_texcoord0) * v_color0;
}
