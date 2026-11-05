$input a_position
$output v_ray

#include <bgfx_shader.sh>

// Globe sky (map_bgfx_globe_surface.h): one triangle covering the screen,
// drawn before the surface. Each corner carries its view ray (ECEF axes):
// forward plus the corner's normalised device coordinates times the
// half-extents of the view (right and up scaled by tan(fov / 2), right also
// by the aspect ratio). The ray is linear across the screen, so the
// interpolated v_ray is every pixel's ray, matching the globe's projection.
uniform vec4 u_sky_forward;
uniform vec4 u_sky_right;
uniform vec4 u_sky_up;

void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_ray = u_sky_forward.xyz + u_sky_right.xyz * a_position.x + u_sky_up.xyz * a_position.y;
}
