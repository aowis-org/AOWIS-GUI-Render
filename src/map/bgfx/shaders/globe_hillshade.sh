// Hillshading of the globe terrain (vs/fs_globe_surface, *_heatmap).
//
// The vertex carries the terrain normal (ECEF, packed into unsigned
// normalized bytes, MapGlobePackedNormal); the light is given in each
// point's own east/north/up frame (u_globe_light: the cartographic
// north-west light 45 degrees up), turned into ECEF per vertex from the
// point's up direction, i.e. its position (relative to the render origin
// u_globe_origin) from the Earth's centre. The fragment scales the imagery by
// lambert / sin(altitude): 1 on flat ground, brighter on slopes facing the
// light, darker on the others, blended in by u_globe_shading.x (0: off).

uniform vec4 u_globe_origin;
uniform vec4 u_globe_light;
uniform vec4 u_globe_shading;

vec3 globeHillshadeNormal(vec4 packed_normal)
{
    return packed_normal.xyz * 2.0 - 1.0;
}

vec3 globeHillshadeLight(vec3 position)
{
    vec3 up = normalize(position + u_globe_origin.xyz);
    vec3 east = vec3(-up.y, up.x, 0.0);
    float east_length = length(east);
    // At the poles any horizontal direction will do.
    east = east_length > 1.0e-6 ? east / east_length : vec3(1.0, 0.0, 0.0);
    vec3 north = cross(up, east);
    return u_globe_light.x * east + u_globe_light.y * north + u_globe_light.z * up;
}

vec3 globeHillshade(vec3 rgb, vec3 normal, vec3 light)
{
    float lambert = max(dot(normalize(normal), normalize(light)), 0.0);
    float shade = lambert / max(u_globe_shading.y, 1.0e-3);
    return rgb * mix(1.0, shade, clamp(u_globe_shading.x, 0.0, 1.0));
}
