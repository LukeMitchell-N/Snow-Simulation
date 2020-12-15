#include "loadobjfile.cpp"
#include "glslProgram.cpp"
#include <vector>

#include "cl_gl_interop.cpp"

#define PI 3.14159265359

struct rgb {
	float r, g, b;
	rgb(float x, float y, float z) {
		r = x;
		g = y;
		b = z;
	}
};

float GroundScale = 6.;
float sceneWidth = 8.;

int yearLength = 30000;	//30 second year cycle
float tempRange = 20;
float avgTemp = -5.;
float currentTemp;
float animationTime;



GLuint Ground, Tree, Cabin, Snow;
float TreeScale = .01;
std::vector<Vertex*> GroundPolys;
std::vector<Vertex*> TreePolys;

std::vector<Vertex> treePositions;
std::vector<float> treeSizes;

struct rgb GroundColor(29. / 255., 140 / 255., 33. / 255.);

GLSLProgram *snowShaders;
const int snowMeshDimension = 128;
volatile struct xyzw* snowPoints;	//snowpoints keeps track of the positions of the first 8 snowflakes to land in each quad of the snow mesh


void initSnowScene();
void initSnowMesh(std::vector<Vertex*> GroundPolys);
void initShaders();
void generateTreePositions();

bool findYBarycentric(Vertex*, Vertex, Vertex, Vertex);
bool isPointInTriangle(Vertex, Vertex, Vertex, Vertex);
bool pointsOnSameSide(Vertex, Vertex, Vertex, Vertex);
void findGroundY(Vertex *v);

void drawScene();
void drawTrees();
void animateScene();

GLfloat* Array3(float, float, float);
float* MulArray3(float factor, float array0[3]);



void initSnowScene() {

	Ground = glGenLists(1);
	GroundPolys = std::vector<Vertex*>(1000);
	GroundPolys.clear();
	glNewList(Ground, GL_COMPILE);
	LoadObjFile("../Models/Ground/SceneGround-LowPoly.obj", &GroundPolys);
	glEndList();
	printf("\n\nGroundPoly has return with %d faces.", GroundPolys.size());

	initFaces(GroundPolys);		//copy the face vertex info to the graphics card
	
	initSnowMesh(GroundPolys);	//create a fine quad mesh for the snow accumulation
	

	initShaders();

	Tree = glGenLists(1);
	glNewList(Tree, GL_COMPILE);
	LoadObjFile("../Models/Tree/CartoonTree.obj", &TreePolys);
	glEndList();

	srand(time(NULL));
	generateTreePositions();

	//snowPoints = new volatile xyzw[snowMeshDimension * snowMeshDimension * 8];
	InitCL(snowPoints, snowMeshDimension, GroundPolys.size());

	//make this copy of the snow points refer to the real one
	snowPoints = &landingPosPtr[0];
	
	
}


void initSnowMesh(std::vector<Vertex*> GroundPolys) {

	Snow = glGenLists(1);
	glNewList(Snow, GL_COMPILE);

	Vertex v1, v2, v3, v4;

	glEnable(GL_NORMALIZE);

	glBegin(GL_QUADS);
	glColor4f(0., 0., 0., 0.);
	float d = float(snowMeshDimension);

	for (int i = 0; i < snowMeshDimension - 1; i++) {
		for (int j = 0; j < snowMeshDimension - 1; j++) {
			float ii = (float)i;
			float jj = (float)j;

			v1.x = ii / d * 2 - 1;
			v1.z = jj / d * 2 - 1;
			findGroundY(&v1);
			glTexCoord2f(ii / d, jj / d);
			glVertex3f(v1.x, v1.y, v1.z);

			v2.x = ii / d * 2 - 1;
			v2.z = (jj + 1) / d * 2 - 1;
			findGroundY(&v2);
			glTexCoord2f(ii / d, jj + 1 / d);
			glVertex3f(v2.x, v2.y, v2.z);

			v3.x = (ii + 1) / d * 2 - 1;
			v3.z = (jj + 1) / d * 2 - 1;
			findGroundY(&v3);
			glTexCoord2f(ii + 1 / d, jj + 1 / d);
			glVertex3f(v3.x, v3.y, v3.z);

			v4.x = (ii + 1) / d * 2 - 1;
			v4.z = jj / d * 2 - 1;
			findGroundY(&v4);
			glTexCoord2f(ii + 1 / d, jj / d);
			glVertex3f(v4.x, v4.y, v4.z);
			/*printf("q2 x, y, z are %f,%f,%f\n", pt.x, pt.y, pt.z);
			printf("q2 s and t are %f,%f\n\n", ii / d, jj / d);*/

			float diag1[3] = { v1.x - v3.x, v1.y - v3.y , v1.z - v3.z };
			float diag2[3] = { v2.x - v4 .x, v2.y - v4.y , v2.z - v4.z };
			float normal[3];
			Cross(diag1, diag2, normal);
			
			glNormal3f(normal[0], normal[1], normal[2]);

		}
	}
	glEnd();
	
	glEndList();

}

void initShaders() {
	snowShaders = new GLSLProgram();
	bool valid = snowShaders->Create("snowShader.vert", "snowShader.frag");
	if (!valid)
	{
		printf("Could not load shaders");
	}
}



void animateScene() {

	int ms = glutGet(GLUT_ELAPSED_TIME);
	ms %= yearLength;
	animationTime = (float)ms / (float)yearLength;

	currentTemp = sin(animationTime * 2 * PI) * tempRange + avgTemp;
	//printf("Current temp: %f", currentTemp);


	//come back with more time to make snow stop
	if (currentTemp <= 0)
		animateSnow();

}


void generateTreePositions() {


	//pick random percentage for the x and the z position
	//	The bottom right corner cannot contain any trees
	//pick random scale factor
	//Each tree will be a small heat source, taking into accound its scale factor
	//	This will mean that no snow appears near the base and gradually more appears until the edge of its needles

	float noTreeLowerX = 1, noTreeLowerZ = 1;
	float shadowFactor = .25;
	bool stopCondition = false, tooClose;
	int attempts = 0, treesPlaced = 0;

	do {
		//pick tree location
		float x = (rand() % 9800 + 100) / 10000.;
		float z = (rand() % 9200 + 400) / 10000.;
		float scaleFactor = ((rand() % 10000) / 8000. + .5) * TreeScale;
		float thisTreeRadius = shadowFactor * scaleFactor;
		tooClose = false;
		attempts++;

		//check location not in no-tree zone
		if (!(x > noTreeLowerX && z > noTreeLowerZ)) {

			//check each tree to see if its too close
			for (int i = 0; i < treePositions.size(); i++) {
				float distanceSquared = (x - treePositions[i].x) * (x - treePositions[i].x) + (z - treePositions[i].z) * (z - treePositions[i].z);

				float thatTreeRadius = shadowFactor * treeSizes[i];
				//if the needles of these two trees overlap
				if (distanceSquared < ((thisTreeRadius + thatTreeRadius) * (thisTreeRadius + thatTreeRadius))) {
					tooClose = true;
					goto fail;
				}
			}
		}
		else {
			goto fail;
		}

		Vertex position;
		position.x = x * 2. - 1.;
		position.z = z * 2. - 1.;
		position.y = 0;
		findGroundY(&position);
		treePositions.push_back(position);
		treeSizes.push_back(scaleFactor);
		treesPlaced++;
		attempts = 0;

	fail:
		if ((tooClose && attempts >= 20) || treesPlaced >= 500) {
			stopCondition = true;
		}
	} while (!stopCondition);
}

void findGroundY(Vertex *v) {

	int polyCount = GroundPolys.size();
	for (int i = 0; i < polyCount; i++) {

		//only check the upper surface of the ground (not the lower face of the ground-box)
		if (GroundPolys[i][0].y > -.14 && GroundPolys[i][1].y > -.14 && GroundPolys[i][2].y > -.14) {

			//barycentric method
			if (findYBarycentric(v, GroundPolys[i][0], GroundPolys[i][1], GroundPolys[i][2]))
				return;
		}
		
	}
	v->y = -.14;

}


bool findYBarycentric(Vertex *p, Vertex t1, Vertex t2, Vertex t3) {

	float denominator = (t2.z - t3.z) * (t1.x - t3.x) + (t3.x - t2.x) * (t1.z - t3.z);
	float weight1 = ((t2.z - t3.z) * (p->x - t3.x) + (t3.x - t2.x) * (p->z - t3.z)) / denominator;
	float weight2 = ((t3.z - t1.z) * (p->x - t3.x) + (t1.x - t3.x) * (p->z - t3.z)) / denominator;
	float weight3 = 1 - weight1 - weight2;

	if (weight1 < 0 || weight2 < 0 || weight3 < 0)
		return false;

	p->y = .01 + weight1 * t1.y + weight2 * t2.y + weight3 * t3.y;
	return true;

}


bool isPointInTriangle(Vertex p, Vertex t1, Vertex t2, Vertex t3) {


	//if the point is on the same side of the line made by two points as the third point, for all points, then it must be in the triangle
	return (
		pointsOnSameSide(p, t1, t2, t3) &&
		pointsOnSameSide(p, t2, t1, t3) &&
		pointsOnSameSide(p, t3, t1, t2));

}

//Algorithm provided by BlackPawn.com
//https://blackpawn.com/texts/pointinpoly/
//not using anymore
bool pointsOnSameSide(Vertex test1, Vertex test2, Vertex line1, Vertex line2) {
	float line[3] = {		line2.x - line1.x,
							0, line2.z - line1.z };
	float t1ToLine[3] = {	test1.x - line1.x,
							0, test1.z - line1.z };
	float t2ToLine[3] = {	test2.x - line1.x,
							0, test2.z - line1.z };

	float cross1[3]; 
	float cross2[3];
	Cross(line, t1ToLine, cross1);
	Cross(line, t2ToLine, cross2);

	float dot = cross1[0] * cross2[0] + cross1[1] * cross2[1] + cross1[2] * cross2[2];

	if (dot >= 0) 
		return true;
	return false;
}


void drawTrees() {
	for (int i = 0; i < treePositions.size(); i++) {
		//printf("This Tree's locations is (%f, %f) \n", treePositions[i][0] - .5, treePositions[i][2] - .5);
		glPushMatrix();
		//glDisable(GL_COLOR);
		glTranslatef(treePositions[i].x , treePositions[i].y, treePositions[i].z);
		glScalef(treeSizes[i], treeSizes[i], treeSizes[i]);
		//glScalef(TreeScale, TreeScale, TreeScale);
		glCallList(Tree);
		glPopMatrix();
	}
}


void drawScene() {

	glEnable(GL_LIGHT1);
	glLightfv(GL_LIGHT1, GL_DIFFUSE, Array3(.8, .7, .3));
	glLightfv(GL_LIGHT1, GL_SPECULAR, Array3(1, 1, 1));
	glLightfv(GL_LIGHT1, GL_POSITION, Array3(0., .2, 0));
	



	glShadeModel(GL_FLAT);

	

	// draw the ground:
	glPushMatrix();
	glScalef(GroundScale, GroundScale, GroundScale);

	glPushMatrix();
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, Array3(GroundColor.r, GroundColor.g, GroundColor.b));
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, Array3(GroundColor.r, GroundColor.g, GroundColor.b));
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, MulArray3(.3, Array3(1, 1, 1)));
	glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 5);
	glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, Array3(0., 0., 0.));

	//glCallList(Ground);
	glPopMatrix();
	glEnable(GL_COLOR);

	//draw the trees
	//printf("treePositions has %d items", treePositions.size());
	//drawTrees();

	//display the snow particles
	//displaySnow();

	glDisable(GL_LIGHTING);
	//draw the snow accumulation with shaders
	float snowHeight = .01;
	float* pointArr = (float*)snowPoints;
	snowShaders->Use();

	GLint shLoc = glGetUniformLocation(snowShaders->Program, "snowHeight");
	glUniform1f(shLoc, snowHeight);
	GLint dLoc = glGetUniformLocation(snowShaders->Program, "meshDimension");
	glUniform1f(dLoc, snowMeshDimension);
	GLint pointsLoc = glGetUniformLocation(snowShaders->Program, "snowPoints");
	glUniform1f(pointsLoc, *pointArr);
	GLint tempLoc = glGetUniformLocation(snowShaders->Program, "currentTemp");
	glUniform1f(tempLoc, currentTemp);
	
	
	glCallList(Snow);

	snowShaders->Use(0);

	glEnable(GL_LIGHTING);

	glPopMatrix();

}


//Dr. Bailey's helper Functions

GLfloat*
Array3(float a, float b, float c)
{
	static float array[4];
	array[0] = a;
	array[1] = b;
	array[2] = c;
	array[3] = 1.;
	return array;
}


float*
MulArray3(float factor, float array0[3])
{
	static float array[4];
	array[0] = factor * array0[0];
	array[1] = factor * array0[1];
	array[2] = factor * array0[2];
	array[3] = 1.;
	return array;
}
