uniform vec4 bigColor;
varying vec4 BaseColor;
uniform float d;
uniform float e;

void main()
{
    vec4 color = BaseColor;

    if (color.x < d) {
        while (color.y < d)
            color += bigColor;
    } else {
        while (color.z < e)
            color.z += bigColor.z;
    }

    gl_FragColor = color;
}
