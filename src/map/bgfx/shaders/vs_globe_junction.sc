$input a_position, a_texcoord1, a_color0, a_texcoord2
$output v_color0, v_corner, v_world, v_sphere

#include <bgfx_shader.sh>
#include "entity_style.sh"
#include "globe_network.sh"

// Junction sphere impostor (MapGlobeJunctionInstance expanded to six
// vertices). a_position: sphere centre; a_texcoord1: corner (-1..1);
// a_texcoord2: radius in metres (used with metre node sizes; pixel sizes are
// converted at the orbit distance); a_color0: the entity reference
// (entity_style.sh). Colour as the QRhi junction shader with its style
// table: diagnostics red (grey while stale), else the selection cyan, else
// the node colour; hidden junctions collapse.
void main()
{
    vec4 texel = entityNodeTexel(a_color0.x);
    vec4 color = entityDiagnosticColor(texel);
    if (color.a < 0.5)
    {
        color = entityHasFlag(texel, ENTITY_FLAG_SELECTED)
            ? vec4(u_entity_colors[ENTITY_COLOR_SELECTION].rgb, 1.0)
            : vec4(entityNodeColor(texel), 1.0);
    }
    if (entityHasFlag(texel, ENTITY_FLAG_HIDDEN))
        color.a = 0.0;
    vec2 viewport = netViewport();
    float radius_world = a_texcoord2;
    if (u_net_viewport.w >= 0.0)
    {
        float reference_depth = max(u_net_params.x, 0.000001);
        radius_world = 2.0 * u_net_viewport.w * reference_depth * NET_TAN_HALF_FOV / viewport.y;
    }

    vec3 eye_to_center = a_position - u_net_camera_eye.xyz;
    float center_distance = max(length(eye_to_center), 0.000001);
    vec3 view_direction = eye_to_center / center_distance;
    float safe_radius = min(max(radius_world, 0.0), center_distance * 0.9999);

    // The quad lies on the sphere's tangent-cone cross section: just large
    // enough to contain the analytic silhouette, also close to the camera;
    // the fragment shader rejects its corners.
    float tangent_denominator = sqrt(max(center_distance * center_distance - safe_radius * safe_radius,
                                         center_distance * center_distance * 0.000001));
    float tangent_radius = safe_radius * center_distance / tangent_denominator;

    // The camera axes reoriented onto the plane perpendicular to this
    // sphere's centre ray (Gram-Schmidt).
    vec3 billboard_right = u_net_camera_right.xyz - view_direction * dot(u_net_camera_right.xyz, view_direction);
    float right_length = length(billboard_right);
    if (right_length <= 0.000001)
        billboard_right = normalize(cross(u_net_camera_up.xyz, view_direction));
    else
        billboard_right /= right_length;
    vec3 billboard_up = u_net_camera_up.xyz - view_direction * dot(u_net_camera_up.xyz, view_direction);
    billboard_up -= billboard_right * dot(billboard_up, billboard_right);
    float up_length = length(billboard_up);
    if (up_length <= 0.000001)
    {
        billboard_up = normalize(cross(view_direction, billboard_right));
        if (dot(billboard_up, u_net_camera_up.xyz) < 0.0)
            billboard_up = -billboard_up;
    }
    else
    {
        billboard_up /= up_length;
    }

    vec3 world_position = a_position
        + tangent_radius * step(0.001, color.a) * (billboard_right * a_texcoord1.x + billboard_up * a_texcoord1.y);
    gl_Position = mul(u_net_view_proj, vec4(world_position, 1.0));
    v_color0 = color;
    v_corner = a_texcoord1;
    v_world = world_position;
    v_sphere = vec4(a_position, safe_radius);
}
