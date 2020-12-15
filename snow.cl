typedef float4 point;
typedef float4 vector;
typedef float4 color;
typedef float4 sphere;



float findYBarycentric(point *p, point t1, point t2, point t3) {

	float denominator = (t2.z - t3.z) * (t1.x - t3.x) + (t3.x - t2.x) * (t1.z - t3.z);
	float weight1 = ((t2.z - t3.z) * (p->x - t3.x) + (t3.x - t2.x) * (p->z - t3.z)) / denominator;
	float weight2 = ((t3.z - t1.z) * (p->x - t3.x) + (t1.x - t3.x) * (p->z - t3.z)) / denominator;
	float weight3 = 1 - weight1 - weight2;

	if (weight1 < 0 || weight2 < 0 || weight3 < 0)
		return -1;
		
	float landedY = weight1 * t1.y + weight2 * t2.y + weight3 * t3.y;
	
	if (landedY >= p->y){
		return landedY;
	}
	else
		return -1.;

		
}

void handleLanding(point* p, global point* dFaces, global point* snowPoints, int gid){
//**********  putting handleLandind code here as a gross workaround
	if (p->y < .6){
		int numFaces = dFaces[0].w;
		for (int i = 0; i < numFaces; i++) {

			//only check the upper surface of the ground (not the lower face of the ground-box)
			if (dFaces[i * 3].y > -.14 && dFaces[i * 3 + 1].y > -.14 && dFaces[i * 3 + 2].y > -.14) {

				// use the barycentric method
				float landedY = findYBarycentric(p, dFaces[i * 3], dFaces[i * 3 + 1], dFaces[i * 3 + 2]);
				//printf("landedY = %f", landedY);
				if (landedY != -1.){
					
					//use pposition of x and z (both - 1 to 1?) to determing the lower vertex of the snowMesh that will contain this ppoint
					
					float meshSize = 128.;						//be careful with this, come back if i have time
					int y = (p->z + 1) / 2 * meshSize;
					int x = (p->x + 1) / 2 * meshSize;
					int index =  (y * meshSize + x) * 8;
					//printf("index of quad is %d\n", index);
					snowPoints[index].w += 1;					//setting w for the quad raises it's pposition in the vertex shader
					
					int writeToIndex = -1;
					for (int j =0; j < 8; j++){					//seek out an unwritten snow location in this quad
						if (snowPoints[index + j].w <= 1)
							writeToIndex = index + j;
					}

					//printf("index for writing is %d\n", writeToIndex);

					if (writeToIndex != -1){					//if there aren't already 8 snowflakes recorded as having fallen in this quad, record this position
						snowPoints[gid] = *p;
						snowPoints[gid].w += 1;
					}
					
					p->y += 2;
				}
					
			}
		}
	}
	//catch those that fell outside the ground box
	if (p->y <= -.14)
		p->y += 2;

}



void updatePosition(point* p, vector v, float DT){
	p->y = p->y + v.y * DT;
}


kernel
void
Particle( global point *dPobj, global vector *dVel, global color *dCobj, global point *dFaces, __global point* snowPoints)
{
	const float  DT      = 0.01;

	int gid = get_global_id( 0 );


	// extract the position and velocity for this particle:
	point  p = dPobj[gid];
	vector v = dVel[gid];


	// advance the particle:
	updatePosition(&p, v, DT);
	
	//save it back into the global array
	dPobj[gid] = p;


	//add test to see if it has made contact with a surface
	handleLanding(&p, dFaces, snowPoints, gid);

	


	
	//test for memory transfer
	//why does this work fine but nothing else will?
	snowPoints[19].x = 30;
	

	dPobj[gid] = p;
}
