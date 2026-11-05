$input v_color0, v_corner, v_world, v_sphere

#include <bgfx_shader.sh>

// Buried junction in X-Ray mode, drawn through the terrain: the symbology
// colour dithered in 3 px screen cells at reduced opacity.
void main()
{
    float radial_distance = length(v_corner);
    float edge_width = max(fwidth(radial_distance), 0.0001);
    float coverage = 1.0 - smoothstep(1.0 - edge_width, 1.0 + edge_width, radial_distance);
    vec2 cell = floor(gl_FragCoord.xy / 3.0);
    if (mod(cell.x + cell.y, 2.0) > 0.5)
        discard;
    float alpha = v_color0.a * coverage * 0.62;
    if (alpha <= 0.001 || v_sphere.w <= 0.0)
        discard;
    gl_FragColor = vec4(v_color0.rgb * alpha, alpha);
}
