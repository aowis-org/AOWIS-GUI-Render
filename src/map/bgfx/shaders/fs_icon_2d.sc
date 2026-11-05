$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_icon_atlas, 0);

// Same colouring as the former QRhi icon shader: the atlas is
// premultiplied; its saturated "fill" pixels take the symbology colour, the
// neutral outline/detail pixels keep the atlas colour. Output is straight
// alpha for BGFX_STATE_BLEND_ALPHA.
void main()
{
    vec4 sampled = texture2D(s_icon_atlas, v_texcoord0);
    float alpha = v_color0.a * sampled.a;
    if (alpha <= 0.001)
        discard;
    vec3 atlas_color = sampled.rgb / max(sampled.a, 0.001);
    float maximum_channel = max(atlas_color.r, max(atlas_color.g, atlas_color.b));
    float minimum_channel = min(atlas_color.r, min(atlas_color.g, atlas_color.b));
    float fill_mask = smoothstep(0.08, 0.20, maximum_channel - minimum_channel);
    gl_FragColor = vec4(mix(atlas_color, v_color0.rgb, fill_mask), alpha);
}
