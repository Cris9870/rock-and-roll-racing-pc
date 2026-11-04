// Scale2x / AdvMAME2x edge rule (Andrea Mazzoleni): each pixel becomes 2x2,
// and a corner takes a neighbour's colour where two neighbours agree, which
// turns 45-degree stair steps into straight diagonals. Written for this port.

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
in vec2 vTexCoord;
out vec4 FragColor;

bool same(vec3 a, vec3 b) { return all(lessThan(abs(a - b), vec3(1.0 / 512.0))); }

void main() {
    vec2 ps = 1.0 / TextureSize;
    vec2 pos = vTexCoord * TextureSize;
    vec2 c = (floor(pos) + 0.5) * ps;
    vec2 sub = fract(pos);
    vec3 B = texture(Texture, c + vec2(0.0, -ps.y)).rgb;
    vec3 D = texture(Texture, c + vec2(-ps.x, 0.0)).rgb;
    vec3 E = texture(Texture, c).rgb;
    vec3 F = texture(Texture, c + vec2(ps.x, 0.0)).rgb;
    vec3 H = texture(Texture, c + vec2(0.0, ps.y)).rgb;
    vec3 r = E;
    if (!same(B, H) && !same(D, F)) {
        if (sub.y < 0.5) {
            if (sub.x < 0.5) { if (same(D, B)) r = D; }
            else             { if (same(B, F)) r = F; }
        } else {
            if (sub.x < 0.5) { if (same(D, H)) r = D; }
            else             { if (same(H, F)) r = F; }
        }
    }
    FragColor = vec4(r, 1.0);
}
#endif
