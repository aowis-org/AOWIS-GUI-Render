$input v_color0, v_corner, v_world, v_sphere

#include <bgfx_shader.sh>
#include "globe_network.sh"

// Junction sphere impostor: antialiased disc whose depth is that of the
// real sphere surface under the pixel, so it occludes like a mesh.
void main()
{
    float radial_distance = length(v_corner);
    float edge_width = max(fwidth(radial_distance), 0.0001);
    float coverage = 1.0 - smoothstep(1.0 - edge_width, 1.0 + edge_width, radial_distance);
    float alpha = v_color0.a * coverage;
    float sphere_radius = v_sphere.w;
    if (alpha <= 0.001 || sphere_radius <= 0.0)
        discard;

    vec3 ray_vector = v_world - u_net_camera_eye.xyz;
    float ray_length = length(ray_vector);
    if (ray_length <= 0.000001)
        discard;
    vec3 ray_direction = ray_vector / ray_length;
    vec3 eye_to_center = u_net_camera_eye.xyz - v_sphere.xyz;
    float ray_projection = dot(eye_to_center, ray_direction);
    // The one-pixel antialias fringe is clamped to the tangent point
    // instead of discarded; coverage fades it out at the silhouette.
    float discriminant = max(ray_projection * ray_projection
                                 - (dot(eye_to_center, eye_to_center) - sphere_radius * sphere_radius),
                             0.0);
    float root = sqrt(discriminant);
    float hit_distance = -ray_projection - root;
    if (hit_distance < 0.0)
        hit_distance = -ray_projection + root;
    if (hit_distance < 0.0)
        discard;

    vec3 surface_position = u_net_camera_eye.xyz + ray_direction * hit_distance;
    vec4 surface_clip = mul(u_net_view_proj, vec4(surface_position, 1.0));
    gl_FragDepth = clamp(surface_clip.z / surface_clip.w * u_net_params.z + u_net_params.w, 0.0, 1.0);
    gl_FragColor = vec4(v_color0.rgb * alpha, alpha);
}
