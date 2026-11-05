$input v_ray

#include <bgfx_shader.sh>

// Globe sky: the light the atmosphere scatters towards the eye along each
// view ray, added onto the space colour (additive blending); the surface is
// drawn over it. A spherical atmosphere around the Earth's centre (radius:
// the sea-level radius under the eye) with an exponential Rayleigh layer
// (scale height 8 km, wavelength-dependent scattering: blue sky) and a thin
// Mie haze layer (1.2 km, grey: the white horizon), up to 100 km. Along the
// ray inside the atmosphere (to the ground or out of it) the optical depth
// of both layers is integrated (24 samples); the in-scattered light of an
// evenly lit atmosphere is then 1 - exp(-beta * depth) per channel, mapped
// so a long path (horizon, limb) reaches the pale horizon colour and the
// short vertical path a deep blue. So, without any special case, the sky is
// blue from the ground, darkens towards black with altitude, is gone above
// the atmosphere except for the thin bright band along the globe's limb,
// and never covers the globe seen from space. Lengths in km (float range).
uniform vec4 u_sky_vertical;  // xyz: geocentric up at the eye (unit)
uniform vec4 u_sky_params;    // x: eye height above the sphere [km], y: sphere radius [km]

#define SKY_TOP_KM 100.0
#define SKY_RAYLEIGH_SCALE_KM 8.0
#define SKY_MIE_SCALE_KM 1.2
#define SKY_SAMPLES 24

void main()
{
    vec3 direction = normalize(v_ray);
    float height = max(u_sky_params.x, 0.001);
    float radius = u_sky_params.y;
    float eye_radius = radius + height;
    // Distance t along the ray: |eye + t d|^2 = r0^2 + 2 b t + t^2.
    float b = eye_radius * dot(direction, u_sky_vertical.xyz);

    // Top of the atmosphere: r0^2 - top^2 written as a product (precise).
    float top_discriminant = b * b - (height - SKY_TOP_KM) * (2.0 * radius + height + SKY_TOP_KM);
    if (top_discriminant <= 0.0)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    float top_root = sqrt(top_discriminant);
    float t_begin = max(0.0, -b - top_root);
    float t_end = -b + top_root;
    // The ground (the sea-level sphere) ends a downward ray.
    float ground_discriminant = b * b - height * (2.0 * radius + height);
    if (b < 0.0 && ground_discriminant > 0.0)
        t_end = min(t_end, -b - sqrt(ground_discriminant));
    if (t_end <= t_begin)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float step_km = (t_end - t_begin) / float(SKY_SAMPLES);
    float rayleigh_depth = 0.0;
    float mie_depth = 0.0;
    for (int index = 0; index < SKY_SAMPLES; ++index)
    {
        float t = t_begin + (float(index) + 0.5) * step_km;
        // Height at t without cancelling two Earth radii against each other.
        float grow = 2.0 * b * t + t * t;
        float sample_height = max(height + grow / (sqrt(eye_radius * eye_radius + grow) + eye_radius), 0.0);
        rayleigh_depth += exp(-sample_height / SKY_RAYLEIGH_SCALE_KM);
        mie_depth += exp(-sample_height / SKY_MIE_SCALE_KM);
    }
    rayleigh_depth *= step_km;
    mie_depth *= step_km;

    // Rayleigh scattering coefficients at sea level (per km, red / green /
    // blue), Mie haze (per km, grey).
    vec3 rayleigh = vec3(1.0, 1.0, 1.0) - exp(-vec3(5.8e-3, 13.5e-3, 33.1e-3) * rayleigh_depth);
    float haze = 1.0 - exp(-0.021 * mie_depth);
    // Pale horizon colour where the path saturates; 7.5: the zenith's blue.
    vec3 horizon = vec3(0.80, 0.88, 0.97);
    vec3 sky = horizon * (vec3(1.0, 1.0, 1.0) - exp(-7.5 * (rayleigh + vec3(0.6, 0.6, 0.6) * haze)));
    gl_FragColor = vec4(sky, 0.0);
}
