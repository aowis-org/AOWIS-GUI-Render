$input v_normal, v_texcoord0, v_model

#include <bgfx_shader.sh>

SAMPLER2D(s_net_albedo, 0);

// Lighting of the QRhi tank/reservoir shader: key, fill and sky light with
// a soft specular; selection tints the model cyan.
void main()
{
    vec4 albedo = texture2D(s_net_albedo, v_texcoord0);
    if (albedo.a <= 0.01)
        discard;
    vec3 normal = normalize(v_normal);
    vec3 key_direction = normalize(vec3(-0.40, -0.32, 0.86));
    vec3 fill_direction = normalize(vec3(0.55, 0.42, 0.48));
    float key = max(dot(normal, key_direction), 0.0);
    float fill = max(dot(normal, fill_direction), 0.0);
    float sky = clamp(normal.z * 0.5 + 0.5, 0.0, 1.0);
    float lighting = 0.46 + key * 0.42 + fill * 0.14 + sky * 0.10;
    vec3 half_vector = normalize(key_direction + vec3(0.0, 0.0, 1.0));
    float specular = pow(max(dot(normal, half_vector), 0.0), 28.0) * 0.22;
    float height_tint = clamp(v_model.x * 0.012, -0.02, 0.04);
    vec3 base_color = max(albedo.rgb, vec3(0.035, 0.035, 0.035));
    vec3 lit = base_color * (lighting + height_tint) + vec3(specular, specular, specular);
    float selected = clamp(v_model.y, 0.0, 1.0);
    vec3 selection_color = vec3(0.0, 0.745, 1.0);
    vec3 selected_surface = selection_color * (0.78 + key * 0.18 + sky * 0.06) + vec3_splat(specular * 0.24);
    lit = mix(lit, selected_surface, selected);
    gl_FragColor = vec4(clamp(lit, 0.0, 1.0), albedo.a);
}
