$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_net_icons, 0);

// The atlas is premultiplied; its coloured (fill) parts take the symbology
// colour, its grey outlines stay.
void main()
{
    vec4 sampled = texture2D(s_net_icons, v_texcoord0);
    float alpha = v_color0.a * sampled.a;
    if (alpha <= 0.001)
        discard;
    vec3 atlas_color = sampled.a > 0.001 ? sampled.rgb / sampled.a : vec3(0.0, 0.0, 0.0);
    float maximum_channel = max(atlas_color.r, max(atlas_color.g, atlas_color.b));
    float minimum_channel = min(atlas_color.r, min(atlas_color.g, atlas_color.b));
    float fill_mask = smoothstep(0.08, 0.20, maximum_channel - minimum_channel);
    vec3 color = mix(atlas_color, v_color0.rgb, fill_mask);
    gl_FragColor = vec4(color * alpha, alpha);
}
