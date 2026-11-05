$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

// Radial heatmap stamp of the QRhi bake (and the CPU raster,
// MapGlobeHeatmapScene::renderStamps()): opaque up to the solid fraction,
// half alpha at 7/16 of the remaining ring, zero at the radius;
// premultiplied output for source-over accumulation. A negative solid
// fraction clears the cell (drawn without blending).
void main()
{
    if (v_color0.a < 0.0)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    float distance_from_center = length(v_texcoord0);
    if (distance_from_center >= 1.0)
        discard;
    float solid_fraction = clamp(v_color0.a, 0.0, 0.9);
    float half_fraction = solid_fraction + (1.0 - solid_fraction) * 0.4375;
    float half_alpha = 128.0 / 255.0;
    float radial_alpha = 1.0;
    if (distance_from_center > solid_fraction)
    {
        if (distance_from_center <= half_fraction)
        {
            float ratio = (distance_from_center - solid_fraction) / max(half_fraction - solid_fraction, 0.0001);
            radial_alpha = mix(1.0, half_alpha, ratio);
        }
        else
        {
            float ratio = (distance_from_center - half_fraction) / max(1.0 - half_fraction, 0.0001);
            radial_alpha = mix(half_alpha, 0.0, ratio);
        }
    }
    gl_FragColor = vec4(v_color0.rgb * radial_alpha, radial_alpha);
}
