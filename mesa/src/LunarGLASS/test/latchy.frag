uniform vec4 bigColor;
varying vec4 BaseColor;
uniform float d;

void main()
{
    vec4 color = BaseColor;

    do {
        color += bigColor;
        if (color.x < d)
            continue;
        if (color.y < d)
            color.y += d;
        else
            color.x += d;
    } while (color.z < d);


    gl_FragColor = color;
}
