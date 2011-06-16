#version 130
uniform sampler2D texSampler2D;
varying vec4 color;
varying float alpha;

void main()
{
	vec4 texColor = texture2D(texSampler2D, vec2(gl_TexCoord[4] + gl_TexCoord[5]));

	texColor += color;

	texColor.a = alpha;

    gl_FragColor = gl_TexCoord[0] + gl_TexCoord[4] + texColor;
}