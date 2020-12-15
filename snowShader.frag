#version 330 compatibility

uniform float snowHeight;
in vec4 vColor;

void
main( )
{
	//if (vColor.a <= .5)
	//	discard;
	gl_FragColor = vColor;
}