$input a_position, a_texcoord0, a_color0, a_texcoord1
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

// Globe heatmap bake (map_bgfx_globe_surface.h): one radial stamp quad
// (MapGlobeHeatmapStamp) into a tile's 256 x 256 cell of a heatmap atlas
// page. a_position: corner (-1..1); a_texcoord0: stamp centre (tile pixels,
// y down), radius (px), solid fraction (< 0: clear the cell instead);
// a_color0: ramp colour; a_texcoord1: the cell's origin in the page (px).
// x: page size (px). y: +1 when render-target row 0 is the texture's v = 0
// (Direct3D, Metal, Vulkan), -1 when it is v = 1 (OpenGL): the baked cell is
// always sampled with the tile's own v-down texture coordinates.
uniform vec4 u_heat_bake;

void main()
{
    float radius_pixels = max(a_texcoord0.z, 0.0001);
    // Clamped to the cell, as the QRhi bake: a stamp never reaches into a
    // neighbouring cell; the corner is recomputed so the gradient holds at
    // a clamped edge.
    vec2 position_pixels = clamp(a_texcoord0.xy + a_position * radius_pixels,
                                 vec2(0.0, 0.0), vec2(256.0, 256.0));
    vec2 page_uv = (a_texcoord1 + position_pixels) / u_heat_bake.x;
    gl_Position = vec4(page_uv.x * 2.0 - 1.0, u_heat_bake.y * (1.0 - page_uv.y * 2.0), 0.0, 1.0);
    v_texcoord0 = (position_pixels - a_texcoord0.xy) / radius_pixels;
    v_color0 = vec4(a_color0.rgb, a_texcoord0.w);
}
