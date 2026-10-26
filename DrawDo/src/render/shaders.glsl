@vs vs
in vec4 position;
in vec4 color0;

out vec4 color;

void main() {
    gl_Position = position;
    color = color0;
}
@end

@fs fs
in vec4 color;
out vec4 frag_color;

void main() {
    frag_color = color;
}
@end

@program simple vs fs

@vs text_vs
layout(binding=0) uniform text_vs_params {
    vec2 fb_size;
};

in vec2 pos;
in vec2 texcoord0;
in vec4 color0;

out vec2 uv;
out vec4 color;

void main() {
    vec2 ndc = pos / fb_size * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.5, 1.0);
    uv = texcoord0;
    color = color0;
}
@end

@fs text_fs
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec4 color;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(tex, smp), uv) * color;
}
@end

@program text text_vs text_fs

@vs shape_vs
layout(binding=0) uniform shape_params {
    vec2 fb_size;
};
in vec2 pos;
in vec2 uv0;
in vec4 color0;
in float circle0;
out vec2 uv;
out vec4 color;
out float circle;
void main() {
    vec2 ndc = pos / fb_size * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.5, 1.0);
    uv = uv0;
    color = color0;
    circle = circle0;
}
@end
@fs shape_fs
in vec2 uv;
in vec4 color;
in float circle;
out vec4 frag_color;
void main() {
    float alpha = circle > 0.5 ? 1.0 - smoothstep(0.72, 1.0, dot(uv, uv)) : 1.0;
    frag_color = color * alpha;
}
@end
@program shape shape_vs shape_fs
