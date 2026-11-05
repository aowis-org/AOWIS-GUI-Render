$input a_position, a_texcoord0, a_texcoord1, a_color0, a_texcoord2, a_texcoord3
$output v_color0, v_segment, v_segment_size

#include <bgfx_shader.sh>
#include "entity_style.sh"
#include "globe_network.sh"

// Globe pipe segment (MapNetworkLinkVertex, six vertices per segment): a
// screen-space capsule around the projected segment, as the QRhi link
// shader. a_position/a_texcoord0: segment start/end; a_texcoord1: corner
// (x: 0 start, 1 end; y: -1/+1 side); a_texcoord2: size adjustment (px; with
// metre widths a negative value is an absolute half width, the highlight
// outlines); a_texcoord3: the local tangent along which metre widths are
// measured (WGS84 east/north plane). a_color0: the vertex colour, or the
// entity reference (entity_style.sh): pipes, pumps and valves take their
// link colour, demand point attachments their demand point's colour.
void main()
{
    vec4 color = a_color0;
    if (entityReferenced())
    {
        bool link = a_color0.y > 0.5;
        vec4 texel = entityTexel(a_color0.x, link);
        color = vec4(link ? entityLinkColor(texel) : entityNodeColor(texel), 1.0);
        if (entityHasFlag(texel, ENTITY_FLAG_HIDDEN))
            color.a = 0.0;
    }
    vec4 start_clip = mul(u_net_view_proj, vec4(a_position, 1.0));
    vec4 end_clip = mul(u_net_view_proj, vec4(a_texcoord0, 1.0));
    vec2 viewport = netViewport();
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
        // Metre width: project the half width along the local tangent at the
        // segment's midpoint, measured across the projected segment.
        float half_width_world = -configured_half_width;
        vec3 width_direction = a_texcoord3;
        float width_direction_length = length(width_direction);
        if (width_direction_length > 0.000001)
        {
            width_direction /= width_direction_length;
        }
        else
        {
            // No tangent given: the scene's XY-plane perpendicular (the
            // QRhi shader's flat fallback).
            vec3 world_direction = a_texcoord0 - a_position;
            float world_direction_xy_length = length(world_direction.xy);
            vec2 world_normal = world_direction_xy_length > 0.000001
                ? vec2(-world_direction.y, world_direction.x) / world_direction_xy_length
                : vec2(1.0, 0.0);
            width_direction = vec3(world_normal, 0.0);
        }
        vec3 midpoint = mix(a_position, a_texcoord0, 0.5);
        vec4 midpoint_clip = mul(u_net_view_proj, vec4(midpoint, 1.0));
        vec4 width_clip = mul(u_net_view_proj, vec4(midpoint + width_direction * half_width_world, 1.0));
        vec2 width_pixels = (width_clip.xy / width_clip.w - midpoint_clip.xy / midpoint_clip.w)
            * viewport * 0.5;
        half_width = width_direction_length > 0.000001 ? abs(dot(width_pixels, normal))
                                                       : length(width_pixels);
    }
    if (configured_half_width < 0.0 && a_texcoord2 < 0.0)
        half_width = -a_texcoord2;
    else
        half_width = max(half_width + a_texcoord2, 0.0);

    // One pixel of raster margin around the capsule for its antialiased edge.
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
