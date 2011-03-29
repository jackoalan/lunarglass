#version 130

uniform vec4 bigColor;
varying vec4 BaseColor;
uniform float d;
uniform float e;

void main()
{
    vec4 color = BaseColor;

    while (color.x < d) {
        color += bigColor;
        while (color.y < e) {
            ++color.y;
        }
    }

    gl_FragColor = color;
}
