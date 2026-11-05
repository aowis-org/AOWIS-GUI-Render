$input v_color0, v_corner

#include <bgfx_shader.sh>

// Antialiased disc, premultiplied output.
void main()
{
    float radius_squared = dot(v_corner, v_corner);
    float edge_width = max(fwidth(radius_squared), 0.0001);
    float coverage = 1.0 - smoothstep(1.0 - edge_width, 1.0 + edge_width, radius_squared);
    float alpha = v_color0.a * coverage;
    if (alpha <= 0.001)
        discard;
    gl_FragColor = vec4(v_color0.rgb * alpha, alpha);
}
