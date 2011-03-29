#version 130

uniform bool  u_b;
uniform bvec2 u_b2;
uniform bvec3 u_b3;
uniform bvec4 u_b4;

uniform int   u_i;
uniform ivec2 u_i2;
uniform ivec3 u_i3;
uniform ivec4 u_i4;

uniform float u_f;
uniform vec2 u_f2;
uniform vec3 u_f3;
uniform vec4 u_f4;

in bool  i_b;
in bvec2 i_b2;
in bvec3 i_b3;
in bvec4 i_b4;

in int   i_i;
in ivec2 i_i2;
in ivec3 i_i3;
in ivec4 i_i4;

in float i_f;
in vec2 i_f2;
in vec3 i_f3;
in vec4 i_f4;

void main()
{
    bool  b = u_b && i_b;
    bvec2 b2 = bvec2(u_b2.x && i_b2.x && u_b2.y && i_b2.y);

    gl_FragColor = vec4(b);
}
