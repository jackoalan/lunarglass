uniform vec4 bigColor;
varying vec4 BaseColor;
uniform float d;

void main()
{
    vec4 color = BaseColor;

    do {
        color += bigColor;
        if (color.y < d)
            color.y += d;
    } while (color.x < d);

    gl_FragColor = color;
}
