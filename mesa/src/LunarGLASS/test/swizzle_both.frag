uniform float blend;
uniform vec4 u;

varying vec2 t;

void main()
{
    float blendscale = 1.789;

    vec4 w = u;
    vec4 w2 = u;
    vec4 w3 = u;
    vec4 w4 = u;

    w.x = blendscale;
    w.y = blendscale;
    w.z = blendscale;

    w2.wy = t;

    w3.zw = t;

    w4.xz = w.yz;

    mix(w2, w3, w4);
    gl_FragColor = mix(w * w2, w3, w4);
}
