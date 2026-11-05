$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

// x = chevron length px, y = half stroke px.
uniform vec4 u_flow_style;

// Distance from position to the segment start-end. Identifiers must avoid
// HLSL keywords (e.g. "point"): shaderc parses .sc files with its HLSL front
// end for the spirv, dx11 and metal profiles.
float segmentDistance(vec2 position, vec2 start, vec2 end)
{
    vec2 to_position = position - start;
    vec2 segment = end - start;
    float along = clamp(dot(to_position, segment) / dot(segment, segment), 0.0, 1.0);
    return length(to_position - segment * along);
}

// Chevron as two antialiased strokes from the tails to the tip (QRhi shape:
// half width 0.4 x length), in quad-local pixels with x along the flow.
void main()
{
    float chevron_length = u_flow_style.x;
    vec2 tip = vec2(0.5 * chevron_length, 0.0);
    vec2 tail_left = vec2(-0.5 * chevron_length, 0.4 * chevron_length);
    vec2 tail_right = vec2(-0.5 * chevron_length, -0.4 * chevron_length);
    float distance_px = min(segmentDistance(v_texcoord0, tail_left, tip),
                            segmentDistance(v_texcoord0, tail_right, tip));
    float coverage = 1.0 - smoothstep(u_flow_style.y - 0.5, u_flow_style.y + 0.5, distance_px);
    float alpha = v_color0.a * coverage;
    if (alpha <= 0.0)
        discard;
    gl_FragColor = vec4(v_color0.rgb, alpha);
}
