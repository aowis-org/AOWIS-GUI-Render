$input v_color0, v_segment, v_segment_size

#include <bgfx_shader.sh>

// Underground pipe run in X-Ray mode, drawn through the terrain: a
// screen-stable dashed stroke in the symbology hue at reduced opacity, so it
// cannot be mistaken for an exposed pipe.
void main()
{
    float half_width = v_segment_size.y;
    if (half_width <= 0.0)
        discard;
    vec2 local = v_segment.xy / v_segment.z;
    float nearest_x = clamp(local.x, 0.0, v_segment_size.x);
    float distance_to_segment = length(vec2(local.x - nearest_x, local.y));
    float edge_width = max(fwidth(distance_to_segment), 0.5);
    float coverage = 1.0 - smoothstep(half_width - edge_width, half_width + edge_width, distance_to_segment);
    float dash = mod(floor((local.x + 2.0) / 7.0), 2.0);
    if (dash > 0.5)
        discard;
    float alpha = v_color0.a * coverage * 0.62;
    if (alpha <= 0.001)
        discard;
    gl_FragColor = vec4(v_color0.rgb * alpha, alpha);
}
