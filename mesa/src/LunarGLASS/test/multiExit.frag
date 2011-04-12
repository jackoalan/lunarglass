uniform vec4 bigColor;
varying vec4 BaseColor;
uniform float d;

void main()
{
    vec4 color = BaseColor;

    do {
       if (d < 0.0)
           break;

       color += bigColor;

       if (d < 1.0)
           break;

       color += BaseColor;

    } while (true);

    gl_FragColor = color;
}
