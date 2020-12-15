#version 330 compatibility

uniform float snowHeight;
uniform float meshDimension;
uniform vec4 snowPoints[];
uniform float currentTemp;
out vec4 vColor;

void
main( )
{
	
	vec4 pos = gl_Vertex;
	pos.y = currentTemp;

	vColor.r = 1;
	vColor.g = 1;
	vColor.b = 1;
	vColor.a = 1;

	gl_Position = gl_ModelViewProjectionMatrix * pos;
}


/*
vec4 pos = gl_Vertex;
	vColor.r = .5;
	vColor.g = 1;
	vColor.b = .5;
	vColor.a = .0004;
	float y = (pos.z + 1) / 2 * meshDimension;
	float x = (pos.x + 1) / 2 * meshDimension;
	int yIndex = int(floor(y));
	int xIndex = int(floor(y));
	int index =  int(yIndex * meshDimension + xIndex) * 8;
	//printf("x: %d, index: %d", x, index);
	


	if(currentTemp <=0){
		vColor.r = 1;
		vColor.g = 1;
		vColor.b = 1;
		vColor.a = 1;
	}
	else{
		vColor.r -= snowHeight;
		vColor.g -= snowHeight;
		vColor.b -= snowHeight;
		vColor.a -= snowHeight;
	}
*/