#version 450
// GTA 2 pre-transformed (D3DTLVERTEX) vertices: screen pixels + rhw.
// w = 1/rhw makes the varyings perspective-correct (D3D TEXTUREPERSPECTIVE);
// no depth: the game draws in painter's order.
layout(location = 0) in vec2 a_pos;     // game screen pixels, D3D pixel centres at integers
layout(location = 1) in float a_w;      // 1 / rhw
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;
layout(push_constant) uniform PC { vec4 xform; } pc;   // ndc = pos * xform.xy + xform.zw
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
void main()
{
    vec2 ndc = a_pos * pc.xform.xy + pc.xform.zw;
    gl_Position = vec4(ndc * a_w, 0.0, a_w);
    v_uv = a_uv;
    v_color = a_color;
}
