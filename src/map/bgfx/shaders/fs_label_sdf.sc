$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

// Map labels from the signed-distance glyph atlas (map_bgfx_glyph_atlas.h),
// vertices from vs_hud (MapBgfxHudCanvas quads, one per glyph).
SAMPLER2D(s_label_sdf, 0);

// Premultiplied halo colour.
uniform vec4 u_label_halo;
// x: halo width (physical px), y: distance range of the atlas (texels,
// MapBgfxGlyphAtlas::SpreadPx), z: fill bias (physical px; > 0 slightly
// emboldens small text), w: unused.
uniform vec4 u_label_params;
// xy: atlas size in texels.
uniform vec4 u_label_atlas;

void main()
{
    // Signed distance to the outline in atlas texels (inside positive).
    float encoded = texture2D(s_label_sdf, v_texcoord0).x;
    float texel_distance = (encoded - 0.5) * 2.0 * u_label_params.y;
    // Atlas texels per screen pixel: labels are axis aligned, so fwidth of
    // one texture axis is exact; averaged over both for robustness.
    vec2 texel = v_texcoord0 * u_label_atlas.xy;
    float texels_per_pixel = max(0.5 * (fwidth(texel.x) + fwidth(texel.y)), 0.0001);
    float pixel_distance = texel_distance / texels_per_pixel + u_label_params.z;

    // One pixel wide antialiased edges for fill and halo.
    float fill = clamp(pixel_distance + 0.5, 0.0, 1.0);
    float halo = clamp(pixel_distance + u_label_params.x + 0.5, 0.0, 1.0);

    // v_color0: premultiplied text colour; its alpha is also the label's
    // fade, which the halo follows. Text over halo, premultiplied.
    vec4 text = v_color0 * fill;
    vec4 outline = u_label_halo * (halo * v_color0.a);
    gl_FragColor = text + outline * (1.0 - text.a);
}
