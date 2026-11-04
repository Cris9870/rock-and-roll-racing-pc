// Edge anti-aliasing in the style of FXAA 2 (Timothy Lottes): finds the
// local edge direction from luma and blends along it, smoothing stair steps
// of any slope. Runs on a prescaled image. Written for this port.

#ifdef VERTEX
in vec2 VertexCoord;
in vec2 TexCoord;
out vec2 vTexCoord;
void main() {
    vTexCoord = TexCoord;
    gl_Position = vec4(VertexCoord * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT
uniform sampler2D Texture;
uniform vec2 TextureSize;
#define FXAA_SPAN 8.0
in vec2 vTexCoord;
out vec4 FragColor;

void main() {
    const vec3 L = vec3(0.299, 0.587, 0.114);
    vec2 rcp = 1.0 / TextureSize;
    vec2 uv = vTexCoord;
    vec3 nw = texture(Texture, uv + vec2(-1.0, -1.0) * rcp).rgb;
    vec3 ne = texture(Texture, uv + vec2( 1.0, -1.0) * rcp).rgb;
    vec3 sw = texture(Texture, uv + vec2(-1.0,  1.0) * rcp).rgb;
    vec3 se = texture(Texture, uv + vec2( 1.0,  1.0) * rcp).rgb;
    vec3 m  = texture(Texture, uv).rgb;
    float lnw = dot(nw, L), lne = dot(ne, L), lsw = dot(sw, L), lse = dot(se, L), lm = dot(m, L);
    float lmin = min(lm, min(min(lnw, lne), min(lsw, lse)));
    float lmax = max(lm, max(max(lnw, lne), max(lsw, lse)));
    vec2 dir = vec2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));
    float reduce = max((lnw + lne + lsw + lse) * (0.25 / 8.0), 1.0 / 128.0);
    float rcp_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcp_min, vec2(-FXAA_SPAN), vec2(FXAA_SPAN)) * rcp;
    vec3 a = 0.5 * (texture(Texture, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                    texture(Texture, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(Texture, uv - dir * 0.5).rgb +
                               texture(Texture, uv + dir * 0.5).rgb);
    float lb = dot(b, L);
    FragColor = vec4((lb < lmin || lb > lmax) ? a : b, 1.0);
}
#endif
