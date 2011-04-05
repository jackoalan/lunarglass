uniform vec4 bigColor;
varying vec4 BaseColor;
uniform float d;

void main()
{
    vec4 color = BaseColor;

    while (true) {
        color += bigColor;
        if (color.x < d)
            break;
        if (color.y < d)
            color.y += d;
        else
            color.x += d;
    }

    gl_FragColor = color;
}
