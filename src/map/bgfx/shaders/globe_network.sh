// Shared uniforms of the bgfx globe network (map_bgfx_globe_network.h),
// the QRhi globe's camera block in bgfx form. Positions are metres relative
// to the network's retained ECEF origin (MapGlobeNetworkScene), the matrix
// is the camera's canonical view-projection for that origin with the
// backend's depth-range correction applied.
uniform mat4 u_net_view_proj;
// xy: logical viewport (px). z: link half width (> 0: px, < 0: -metres).
// w: node radius (> 0: px, < 0: -metres).
uniform vec4 u_net_viewport;
// x: orbit distance (the depth at which pixel sizes are defined).
// y: icon size (> 0: px, < 0: -metres). zw: NDC depth -> window depth
// (scale, offset) for the junction impostors' gl_FragDepth.
uniform vec4 u_net_params;
// Camera basis relative to the network origin.
uniform vec4 u_net_camera_right;
uniform vec4 u_net_camera_up;
uniform vec4 u_net_camera_eye;

// tan(22.5 deg): MapModel::GlobeFieldOfViewDeg / 2.
#define NET_TAN_HALF_FOV 0.4142135623730950

vec2 netViewport()
{
    return max(u_net_viewport.xy, vec2(1.0, 1.0));
}

// The pixel-space segment coordinates are interpolated linearly in screen
// space (independent of the endpoints' depths): passed multiplied by w and
// divided by the interpolated w in the fragment shader. WebGL 2 has no
// noperspective qualifier.
vec4 netScreenLinear(vec2 value, float w)
{
    return vec4(value * w, w, 0.0);
}
