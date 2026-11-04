// Sharp bilinear: integer prescale (nearest) followed by a bilinear step only
// across pixel borders. Even pixel widths at any output size, no shimmer.
// Technique by Themaister (public domain); written for this port.

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
uniform vec2 InputSize;
uniform vec2 OutputSize;
in vec2 vTexCoord;
out vec4 FragColor;
void main() {
    vec2 texel = vTexCoord * TextureSize;
    vec2 scale = max(floor(OutputSize / InputSize), vec2(1.0));
    vec2 base = floor(texel);
    vec2 center_dist = fract(texel) - 0.5;
    vec2 range = 0.5 - 0.5 / scale;
    vec2 f = (center_dist - clamp(center_dist, -range, range)) * scale + 0.5;
    FragColor = vec4(texture(Texture, (base + f) / TextureSize).rgb, 1.0);
}
#endif
