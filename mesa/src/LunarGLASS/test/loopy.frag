#version 130

uniform vec4 bigColor;
varying vec4 BaseColor;
uniform float d;
uniform float e;
uniform float f;
uniform float g;
uniform float h;
uniform float i;
uniform float j;
uniform float k;

uniform int Count;

void main()
{
    vec4 color = BaseColor;

    color.z += bigColor.z;

    while (color.x < d) {
        color += bigColor;

        if (color.w < f)
           break;

        while (color.y < e) {
            ++color.y;
            for (int i = 0; i < Count; ++i) {
                color.z += h;
            }
        }

        if (color.y < e)
           break;
        else
           ++color.w;

       color += vec4(g);

    }

    gl_FragColor = color;
}
