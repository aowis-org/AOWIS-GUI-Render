$input a_position, a_texcoord0, a_texcoord1, a_color0, a_texcoord2
$output v_color0, v_segment, v_segment_size

#include <bgfx_shader.sh>
#include "entity_style.sh"
#include "globe_network.sh"

// Flow-direction chevron stroke (MapGlobeNetworkScene::flowDirectionVertices,
// six vertices per stroke), drawn with fs_globe_link. As the QRhi shader,
// each endpoint is pulled 8 logical pixels towards the eye along its own
// projection ray: the depth changes, the screen position does not, so the
// chevrons stay in front of their pipe without a depth bias state (bgfx has
// none) while terrain and the far side of the globe still occlude them.
vec3 depthSeparatedPosition(vec3 position, float view_depth, vec2 viewport)
{
    vec3 direction_to_eye = u_net_camera_eye.xyz - position;
    float distance_to_eye = length(direction_to_eye);
    if (distance_to_eye <= 0.000001)
        return position;
    float world_units_per_pixel = 2.0 * max(abs(view_depth), 0.000001) * NET_TAN_HALF_FOV / viewport.y;
    return position + direction_to_eye / distance_to_eye * (world_units_per_pixel * 8.0);
}

// Every link with chevrons has them in both orientations (a_color0: the
// entity reference, z the orientation): the link's current flow direction
// (entity_style.sh) shows one set, so a time step never rebuilds them. The
// colour contrasts with the link's.
void main()
{
    vec4 texel = entityLinkTexel(a_color0.x);
    float shown = u_entity_options.z * (1.0 - entityFlag(texel.z, ENTITY_FLAG_HIDDEN))
        * step(abs(texel.y - a_color0.z), 0.5) * abs(texel.y);
    vec4 color = vec4(entityFlowColor(entityLinkColor(texel)), shown);
    vec2 viewport = netViewport();
    vec4 unseparated_start_clip = mul(u_net_view_proj, vec4(a_position, 1.0));
    vec4 unseparated_end_clip = mul(u_net_view_proj, vec4(a_texcoord0, 1.0));
    vec4 start_clip = mul(u_net_view_proj,
                          vec4(depthSeparatedPosition(a_position, unseparated_start_clip.w, viewport), 1.0));
    vec4 end_clip = mul(u_net_view_proj,
                        vec4(depthSeparatedPosition(a_texcoord0, unseparated_end_clip.w, viewport), 1.0));
    vec2 start_ndc = start_clip.xy / start_clip.w;
    vec2 end_ndc = end_clip.xy / end_clip.w;
    vec2 direction_pixels = (end_ndc - start_ndc) * viewport * 0.5;
    float direction_length = length(direction_pixels);
    vec2 tangent = direction_length > 0.0001 ? direction_pixels / direction_length : vec2(1.0, 0.0);
    vec2 normal = vec2(-tangent.y, tangent.x);

    float configured_half_width = u_net_viewport.z;
    float half_width = max(configured_half_width, 0.0);
    if (configured_half_width < 0.0)
    {
        // As the QRhi shader: the metre width is measured in the scene's XY
        // plane (the chevrons carry absolute half widths anyway, below).
        float half_width_world = -configured_half_width;
        vec3 world_direction = a_texcoord0 - a_position;
        float world_direction_length = length(world_direction.xy);
        vec2 world_normal = world_direction_length > 0.000001
            ? vec2(-world_direction.y, world_direction.x) / world_direction_length
            : vec2(1.0, 0.0);
        vec3 midpoint = mix(a_position, a_texcoord0, 0.5);
        vec4 midpoint_clip = mul(u_net_view_proj, vec4(midpoint, 1.0));
        vec4 width_clip = mul(u_net_view_proj, vec4(midpoint + vec3(world_normal * half_width_world, 0.0), 1.0));
        half_width = length((width_clip.xy / width_clip.w - midpoint_clip.xy / midpoint_clip.w)
                            * viewport * 0.5);
    }
    if (configured_half_width < 0.0 && a_texcoord2 < 0.0)
        half_width = -a_texcoord2;
    else
        half_width = max(half_width + a_texcoord2, 0.0);

    // Nothing to draw: a zero-area quad.
    float extent = (half_width + 1.0) * step(0.001, color.a);
    float endpoint_sign = a_texcoord1.x * 2.0 - 1.0;
    vec4 clip_position = mix(start_clip, end_clip, a_texcoord1.x);
    vec2 offset_pixels = tangent * endpoint_sign * extent + normal * a_texcoord1.y * extent;
    clip_position.xy += offset_pixels * 2.0 / viewport * clip_position.w;

    gl_Position = clip_position;
    v_color0 = color;
    v_segment = netScreenLinear(vec2(mix(-extent, direction_length + extent, a_texcoord1.x),
                                     a_texcoord1.y * extent),
                                clip_position.w);
    v_segment_size = vec2(direction_length, half_width);
}
