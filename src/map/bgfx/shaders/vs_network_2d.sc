$input a_position, a_texcoord0, a_texcoord1
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>
#include "entity_style.sh"

// Chunk-local world -> clip: xy = chunk origin in clip, zw = clip per world unit.
uniform vec4 u_network_camera;
// xy = clip per screen pixel (x right, y down).
uniform vec4 u_network_pixel;
// x = marker half size in pixels.
uniform vec4 u_marker_style;

// Node markers and heatmap spots: a_texcoord0 holds unit corners (-1/+1),
// a_texcoord1.x the node's render id. The size is a uniform so a symbology
// size change never rebuilds geometry; the colour comes from the node's data
// (entity_style.sh): the symbology colour, the heatmap ramp (heatmap pass),
// or the selection / diagnostic colour (highlight layers). A marker that is
// switched off collapses to a zero-area quad. The corner is passed on so the
// fragment shader can cut an antialiased circle.
void main()
{
    vec4 texel = entityNodeTexel(a_texcoord1.x);
    vec4 color = vec4(0.0, 0.0, 0.0, 0.0);
    bool hidden_junction = entityHasFlag(texel, ENTITY_FLAG_KIND) && u_entity_options.x < 0.5;
    if (entityMode(ENTITY_MODE_SELECTION))
    {
        if (!hidden_junction)
            color = vec4(u_entity_colors[ENTITY_COLOR_SELECTION].rgb, 1.0);
    }
    else if (entityMode(ENTITY_MODE_DIAGNOSTIC))
    {
        if (!hidden_junction)
            color = entityDiagnosticColor(texel);
    }
    else if (!entityHasFlag(texel, ENTITY_FLAG_HIDDEN))
    {
        if (entityMode(ENTITY_MODE_HEATMAP))
            color = entityHeatColor(texel);
        else if (!entityNodeMarkerOff(texel))
            color = vec4(entityNodeColor(texel), 1.0);
    }

    vec2 clip = u_network_camera.xy + a_position.xy * u_network_camera.zw;
    vec2 corner = a_texcoord0.xy * step(0.001, color.a);
    gl_Position = vec4(clip + corner * u_marker_style.x * u_network_pixel.xy, 0.0, 1.0);
    v_color0 = color;
    v_texcoord0 = a_texcoord0;
}
