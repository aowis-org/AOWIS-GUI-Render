$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

// x = heatmap opacity (0..1), y = solid centre fraction of the radius (0..0.9).
uniform vec4 u_heatmap_style;

// One heatmap spot per node, drawn on the node marker quad (vs_network_2d.sc
// with the radius as size), so v_texcoord0 is the unit corner. Same radial
// profile as the former QRhi heatmap: full strength inside the
// solid centre, linear to 0.5 at 43.75 % of the remaining radius, then
// linear to 0 at the rim. Output is premultiplied (blend One,
// OneMinusSrcAlpha). v_color0.a is 0 for nodes without a heatmap value.
void main()
{
    float distance_from_center = length(v_texcoord0.xy);
    if (distance_from_center >= 1.0)
        discard;

    float solid_fraction = clamp(u_heatmap_style.y, 0.0, 0.9);
    float fade_middle = solid_fraction + (1.0 - solid_fraction) * 0.4375;
    float radial_alpha = 1.0;
    if (distance_from_center > solid_fraction)
    {
        if (distance_from_center <= fade_middle)
        {
            float ratio = (distance_from_center - solid_fraction)
                / max(fade_middle - solid_fraction, 0.0001);
            radial_alpha = mix(1.0, 0.5, ratio);
        }
        else
        {
            float ratio = (distance_from_center - fade_middle)
                / max(1.0 - fade_middle, 0.0001);
            radial_alpha = mix(0.5, 0.0, ratio);
        }
    }

    float alpha = radial_alpha * clamp(u_heatmap_style.x, 0.0, 1.0) * v_color0.a;
    if (alpha <= 0.001)
        discard;
    gl_FragColor = vec4(v_color0.rgb * alpha, alpha);
}
