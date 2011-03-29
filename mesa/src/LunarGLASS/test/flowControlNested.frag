#version 130
uniform float d;
uniform vec4 bigColor, smallColor;
uniform vec4 otherColor;

varying float c;
varying vec4 BaseColor;

void main()
{
    vec4 color = BaseColor;
	vec4 color2;

	color2 = otherColor;

    if (c > d)
       if (c < d + 5)
           color += bigColor;
       else {
           if (c > d - 12)
               color.x += bigColor.y;
           color.y += bigColor.z;
       }

    gl_FragColor = color * color2;
}
