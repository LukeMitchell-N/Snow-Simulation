#pragma comment(lib, "OpenCL32.lib")
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <omp.h>

#define _USE_MATH_DEFINES
#include <math.h>

#ifdef WIN32
#include <windows.h>
#pragma warning(disable:4996)
#endif

#ifdef WIN32
#include "glew.h"
#endif

#include <GL/gl.h>
#include <GL/glu.h>
#include "glut.h"
#include "glui.h"

#include "cl.h"
#include "cl_gl.h"

// opencl vendor ids:
#define ID_AMD		0x1002
#define ID_INTEL	0x8086
#define ID_NVIDIA	0x10de


// random parameters:					

const float XMIN = 	{ -1.0 };
const float XMAX = 	{  1.0 };
const float Y	 = 	{ 2.0 };
const float ZMIN = 	{ -1.0 };
const float ZMAX = 	{  1.0 };


const int NUM_PARTICLES = 1024 * 1024;
const int LOCAL_SIZE    = 128;
const char *CL_FILE_NAME   = { "snow.cl" };
const char *CL_BINARY_NAME = { "snow.nv" };


struct xyzw
{
	float x, y, z, w;
};
struct rgba
{
	float r, g, b, a;
};


size_t				GlobalWorkSize[3] = { NUM_PARTICLES, 1, 1 };
size_t				LocalWorkSize[3] = { LOCAL_SIZE, 1, 1 };
GLuint				hPobj; // host opengl object for Points
GLuint				hCobj; // host opengl object for Colors
struct xyzw* hVel; // host C++ array for Velocities
cl_mem				dPobj; // device memory buffer for Points
cl_mem				dCobj; // device memory buffer for Colors
cl_mem				dVel; // device memory buffer for Velocities

//array of positions of snowflake landings
int					NumVertices;

//array of faces on which snow can land
struct xyzw*		hFaces;
cl_mem				dFaces;

cl_mem				landingPosMem;
volatile xyzw*		landingPosPtr;

cl_command_queue	CmdQueue;
cl_device_id		Device;
cl_kernel			Kernel;
cl_platform_id		Platform;
cl_program			Program;


void	InitCL(struct xyzw*, int, int);
void	PrintCLError(cl_int, char* = "", FILE* = stderr);
void	PrintOpenclInfo();
bool	IsCLExtensionSupported(const char*);
void	SelectOpenclDevice();
void	initParticles();
void	initFaces(std::vector<Vertex*> GroundPolys);
float	Ranf(float, float);
void	animateSnow();
void	displaySnow();


void	animateSnow() {

	cl_int  status;


	status = clEnqueueAcquireGLObjects(CmdQueue, 1, &dPobj, 0, NULL, NULL);
	PrintCLError(status, "clEnqueueAcquireGLObjects (1): ");
	status = clEnqueueAcquireGLObjects(CmdQueue, 1, &dCobj, 0, NULL, NULL);
	PrintCLError(status, "clEnqueueAcquireGLObjects (2): ");

	cl_event wait;
	
	status = clEnqueueNDRangeKernel(CmdQueue, Kernel, 1, NULL, GlobalWorkSize, LocalWorkSize, 0, NULL, &wait);
	PrintCLError(status, "clEnqueueNDRangeKernel: ");
	clFinish(CmdQueue);

	status = clEnqueueReleaseGLObjects(CmdQueue, 1, &dCobj, 0, NULL, NULL);
	PrintCLError(status, "clEnqueueReleaseGLObjects (1): ");
	status = clEnqueueReleaseGLObjects(CmdQueue, 1, &dPobj, 0, NULL, NULL);
	PrintCLError(status, "clEnqueueReleaseGLObjects (2): ");


	glutPostRedisplay();

}


void displaySnow() {


	//glClearColor(0,0,0,0);
	glDisable(GL_LIGHTING);

	glBindBuffer(GL_ARRAY_BUFFER, hPobj);
	glVertexPointer(4, GL_FLOAT, 0, (void*)0);
	glEnableClientState(GL_VERTEX_ARRAY);

	glBindBuffer(GL_ARRAY_BUFFER, hCobj);
	glColorPointer(4, GL_FLOAT, 0, (void*)0);
	glEnableClientState(GL_COLOR_ARRAY);



	glPointSize(1.);
	glDrawArrays(GL_POINTS, 0, NUM_PARTICLES);
	glPointSize(1.);

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glEnable(GL_LIGHTING);

	//printf("Ultimate test1: %f", landingPosPtr[19].x);
}


//
// initialize the opencl stuff:
//
void
InitCL(volatile struct xyzw* snowPoints, int snowMeshDimension, int numFaces)
{
	// show us what we've got here:

	PrintOpenclInfo();

	// pick which opencl device to use
	// this fills the globals Platform and Device:

	SelectOpenclDevice();


	// see if we can even open the opencl Kernel Program
	// (no point going on if we can't):

	FILE* fp = fopen(CL_FILE_NAME, "r");
	if (fp == NULL)
	{
		fprintf(stderr, "Cannot open OpenCL source file '%s'\n", CL_FILE_NAME);
		return;
	}

	// 2. allocate the host memory buffers:

	cl_int status;		// returned status from opencl calls
						// test against CL_SUCCESS

	// since this is an opengl interoperability program,
	// check if the opengl sharing extension is supported,
	// (no point going on if it isn't):
	// (we need the Device in order to ask, so can't do it any sooner than here)

	if (IsCLExtensionSupported("cl_khr_gl_sharing"))
	{
		fprintf(stderr, "cl_khr_gl_sharing is supported.\n");
	}
	else
	{
		fprintf(stderr, "cl_khr_gl_sharing is not supported -- sorry.\n");
		return;
	}



	// 3. create an opencl context based on the opengl context:

	cl_context_properties props[] =
	{
		CL_GL_CONTEXT_KHR,		(cl_context_properties)wglGetCurrentContext(),
		CL_WGL_HDC_KHR,			(cl_context_properties)wglGetCurrentDC(),
		CL_CONTEXT_PLATFORM,		(cl_context_properties)Platform,
		0
	};

	cl_context Context = clCreateContext(props, 1, &Device, NULL, NULL, &status);
	PrintCLError(status, "clCreateContext: ");

	// 4. create an opencl command queue:

	CmdQueue = clCreateCommandQueue(Context, Device, 0, &status);
	if (status != CL_SUCCESS)
		fprintf(stderr, "clCreateCommandQueue failed\n");



	// create the velocity array and the opengl vertex array buffer and color array buffer:
	delete[] hVel;
	hVel = new struct xyzw[NUM_PARTICLES];

	glGenBuffers(1, &hPobj);
	glBindBuffer(GL_ARRAY_BUFFER, hPobj);
	glBufferData(GL_ARRAY_BUFFER, 4 * NUM_PARTICLES * sizeof(float), NULL, GL_STATIC_DRAW);

	glGenBuffers(1, &hCobj);
	glBindBuffer(GL_ARRAY_BUFFER, hCobj);
	glBufferData(GL_ARRAY_BUFFER, 4 * NUM_PARTICLES * sizeof(float), NULL, GL_STATIC_DRAW);


	glBindBuffer(GL_ARRAY_BUFFER, 0);	// unbind the buffer


	// fill those arrays and buffers:
	initParticles();



	

	// 5. create the opencl version of the opengl buffers:

	dPobj = clCreateFromGLBuffer(Context, CL_MEM_READ_WRITE, hPobj, &status);
	PrintCLError(status, "clCreateFromGLBuffer (1)");

	dCobj = clCreateFromGLBuffer(Context, CL_MEM_READ_WRITE, hCobj, &status);
	PrintCLError(status, "clCreateFromGLBuffer (2)");


	// 5. create the opencl version of the velocity array:

	dVel = clCreateBuffer(Context, CL_MEM_READ_WRITE, 4 * sizeof(float) * NUM_PARTICLES, NULL, &status);
	PrintCLError(status, "clCreateBuffer: ");

	//and the mesh faces array
	dFaces = clCreateBuffer(Context, CL_MEM_READ_WRITE, 4 * sizeof(float) * NUM_PARTICLES, NULL, &status);
	PrintCLError(status, "clCreateBuffer: ");

	landingPosMem = clCreateBuffer(Context, CL_MEM_READ_WRITE, 4 * sizeof(GLfloat) * snowMeshDimension * snowMeshDimension* 8 , NULL, &status);
	PrintCLError(status, "clCreateBuffer: ");



	// 6. enqueue the command to write the data from the host buffers to the Device buffers:

	status = clEnqueueWriteBuffer(CmdQueue, dVel, CL_FALSE, 0, 4 * sizeof(float) * NUM_PARTICLES, hVel, 0, NULL, NULL);
	PrintCLError(status, "clEneueueWriteBuffer: ");

	//for Faces too
	status = clEnqueueWriteBuffer(CmdQueue, dFaces, CL_FALSE, 0, 4 * sizeof(float) * NumVertices, hFaces, 0, NULL, NULL);
	PrintCLError(status, "clEneueueWriteBuffer: ");


	// 7. read the Kernel code from a file:

	fseek(fp, 0, SEEK_END);
	size_t fileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	char* clProgramText = new char[fileSize + 1];		// leave room for '\0'
	size_t n = fread(clProgramText, 1, fileSize, fp);
	clProgramText[fileSize] = '\0';
	fclose(fp);

	// create the text for the Kernel Program:

	char* strings[1];
	strings[0] = clProgramText;
	Program = clCreateProgramWithSource(Context, 1, (const char**)strings, NULL, &status);
	if (status != CL_SUCCESS)
		fprintf(stderr, "clCreateProgramWithSource failed\n");
	delete[] clProgramText;

	// 8. compile and link the Kernel code:

	char* options = { "" };
	status = clBuildProgram(Program, 1, &Device, options, NULL, NULL);
	if (status != CL_SUCCESS)
	{
		size_t size;
		clGetProgramBuildInfo(Program, Device, CL_PROGRAM_BUILD_LOG, 0, NULL, &size);
		cl_char* log = new cl_char[size];
		clGetProgramBuildInfo(Program, Device, CL_PROGRAM_BUILD_LOG, size, log, NULL);
		fprintf(stderr, "clBuildProgram failed:\n%s\n", log);
		delete[] log;
	}


	// 9. create the Kernel object:

	Kernel = clCreateKernel(Program, "Particle", &status);
	PrintCLError(status, "clCreateKernel failed: ");


	//testing
	void* output = clEnqueueMapBuffer(CmdQueue, landingPosMem, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE, 0, 4, 0, NULL, NULL, &status);
	PrintCLError(status, "???: "); 
	landingPosPtr = (volatile xyzw*)output;
	landingPosPtr[0].x = 0;
	//snowPoints = landingPosPtr;


	// 10. setup the arguments to the Kernel object:

	status = clSetKernelArg(Kernel, 0, sizeof(cl_mem), &dPobj);
	PrintCLError(status, "clSetKernelArg (1): ");

	status = clSetKernelArg(Kernel, 1, sizeof(cl_mem), &dVel);
	PrintCLError(status, "clSetKernelArg (2): ");

	status = clSetKernelArg(Kernel, 2, sizeof(cl_mem), &dCobj);
	PrintCLError(status, "clSetKernelArg (3): ");

	status = clSetKernelArg(Kernel, 3, sizeof(cl_mem), &dFaces);
	PrintCLError(status, "clSetKernelArg (5): ");

	status = clSetKernelArg(Kernel, 4, sizeof(landingPosMem), &landingPosMem);
	PrintCLError(status, "clSetKernelArg (6): ");

}



void initParticles()
{
	glBindBuffer(GL_ARRAY_BUFFER, hPobj);
	struct xyzw* points = (struct xyzw*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	for (int i = 0; i < NUM_PARTICLES; i++)
	{
		points[i].x = Ranf(XMIN, XMAX);
		points[i].y = Y + Ranf(-.001, .0001);
		points[i].z = Ranf(ZMIN, ZMAX);
		points[i].w = 1.;
	}
	glUnmapBuffer(GL_ARRAY_BUFFER);

	
	glBindBuffer(GL_ARRAY_BUFFER, hCobj);
	struct rgba* colors = (struct rgba*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	for (int i = 0; i < NUM_PARTICLES; i++)
	{
		colors[i].r = 1.;
		colors[i].g = 1.;
		colors[i].b = 1.;
		colors[i].a = 1.;
	}
	glUnmapBuffer(GL_ARRAY_BUFFER);


	for (int i = 0; i < NUM_PARTICLES; i++)
	{
		hVel[i].x = 0;
		hVel[i].y = Ranf(-3, -1);
		hVel[i].z = 0;
	}

}


void initFaces(std::vector<Vertex*> GroundPolys) {
	
	NumVertices = GroundPolys.size() * 3; //3 verts each face

	delete[] hFaces;
	hFaces = new struct xyzw[NumVertices];
	

	for (int i = 0; i < GroundPolys.size(); i++)
	{
		hFaces[i * 3].x = GroundPolys[i][0].x;
		hFaces[i * 3].y = GroundPolys[i][0].y;
		hFaces[i * 3].z = GroundPolys[i][0].z;
		hFaces[i * 3].w = 0;
		hFaces[i * 3 + 1].x = GroundPolys[i][1].x;
		hFaces[i * 3 + 1].y = GroundPolys[i][1].y;
		hFaces[i * 3 + 1].z = GroundPolys[i][1].z;
		hFaces[i * 3 + 1].w = 0; 
		hFaces[i * 3 + 2].x = GroundPolys[i][2].x;
		hFaces[i * 3 + 2].y = GroundPolys[i][2].y;
		hFaces[i * 3 + 2].z = GroundPolys[i][2].z;
		hFaces[i * 3 + 2].w = 0;
	}

	hFaces[0].w = GroundPolys.size();
}




bool
IsCLExtensionSupported(const char* extension)
{
	// see if the extension is bogus:

	if (extension == NULL || extension[0] == '\0')
		return false;

	char* where = (char*)strchr(extension, ' ');
	if (where != NULL)
		return false;

	// get the full list of extensions:

	size_t extensionSize;
	clGetDeviceInfo(Device, CL_DEVICE_EXTENSIONS, 0, NULL, &extensionSize);
	char* extensions = new char[extensionSize];
	clGetDeviceInfo(Device, CL_DEVICE_EXTENSIONS, extensionSize, extensions, NULL);

	for (char* start = extensions; ; )
	{
		where = (char*)strstr((const char*)start, extension);
		if (where == 0)
		{
			delete[] extensions;
			return false;
		}

		char* terminator = where + strlen(extension);	// points to what should be the separator

		if (*terminator == ' ' || *terminator == '\0' || *terminator == '\r' || *terminator == '\n')
		{
			delete[] extensions;
			return true;
		}
		start = terminator;
	}

	delete[] extensions;
	return false;
}


struct errorcode
{
	cl_int		statusCode;
	char* meaning;
}
ErrorCodes[] =
{
	{ CL_SUCCESS,				""					},
	{ CL_DEVICE_NOT_FOUND,			"Device Not Found"			},
	{ CL_DEVICE_NOT_AVAILABLE,		"Device Not Available"			},
	{ CL_COMPILER_NOT_AVAILABLE,		"Compiler Not Available"		},
	{ CL_MEM_OBJECT_ALLOCATION_FAILURE,	"Memory Object Allocation Failure"	},
	{ CL_OUT_OF_RESOURCES,			"Out of resources"			},
	{ CL_OUT_OF_HOST_MEMORY,		"Out of Host Memory"			},
	{ CL_PROFILING_INFO_NOT_AVAILABLE,	"Profiling Information Not Available"	},
	{ CL_MEM_COPY_OVERLAP,			"Memory Copy Overlap"			},
	{ CL_IMAGE_FORMAT_MISMATCH,		"Image Format Mismatch"			},
	{ CL_IMAGE_FORMAT_NOT_SUPPORTED,	"Image Format Not Supported"		},
	{ CL_BUILD_PROGRAM_FAILURE,		"Build Program Failure"			},
	{ CL_MAP_FAILURE,			"Map Failure"				},
	{ CL_INVALID_VALUE,			"Invalid Value"				},
	{ CL_INVALID_DEVICE_TYPE,		"Invalid Device Type"			},
	{ CL_INVALID_PLATFORM,			"Invalid Platform"			},
	{ CL_INVALID_DEVICE,			"Invalid Device"			},
	{ CL_INVALID_CONTEXT,			"Invalid Context"			},
	{ CL_INVALID_QUEUE_PROPERTIES,		"Invalid Queue Properties"		},
	{ CL_INVALID_COMMAND_QUEUE,		"Invalid Command Queue"			},
	{ CL_INVALID_HOST_PTR,			"Invalid Host Pointer"			},
	{ CL_INVALID_MEM_OBJECT,		"Invalid Memory Object"			},
	{ CL_INVALID_IMAGE_FORMAT_DESCRIPTOR,	"Invalid Image Format Descriptor"	},
	{ CL_INVALID_IMAGE_SIZE,		"Invalid Image Size"			},
	{ CL_INVALID_SAMPLER,			"Invalid Sampler"			},
	{ CL_INVALID_BINARY,			"Invalid Binary"			},
	{ CL_INVALID_BUILD_OPTIONS,		"Invalid Build Options"			},
	{ CL_INVALID_PROGRAM,			"Invalid Program"			},
	{ CL_INVALID_PROGRAM_EXECUTABLE,	"Invalid Program Executable"		},
	{ CL_INVALID_KERNEL_NAME,		"Invalid Kernel Name"			},
	{ CL_INVALID_KERNEL_DEFINITION,		"Invalid Kernel Definition"		},
	{ CL_INVALID_KERNEL,			"Invalid Kernel"			},
	{ CL_INVALID_ARG_INDEX,			"Invalid Argument Index"		},
	{ CL_INVALID_ARG_VALUE,			"Invalid Argument Value"		},
	{ CL_INVALID_ARG_SIZE,			"Invalid Argument Size"			},
	{ CL_INVALID_KERNEL_ARGS,		"Invalid Kernel Arguments"		},
	{ CL_INVALID_WORK_DIMENSION,		"Invalid Work Dimension"		},
	{ CL_INVALID_WORK_GROUP_SIZE,		"Invalid Work Group Size"		},
	{ CL_INVALID_WORK_ITEM_SIZE,		"Invalid Work Item Size"		},
	{ CL_INVALID_GLOBAL_OFFSET,		"Invalid Global Offset"			},
	{ CL_INVALID_EVENT_WAIT_LIST,		"Invalid Event Wait List"		},
	{ CL_INVALID_EVENT,			"Invalid Event"				},
	{ CL_INVALID_OPERATION,			"Invalid Operation"			},
	{ CL_INVALID_GL_OBJECT,			"Invalid GL Object"			},
	{ CL_INVALID_BUFFER_SIZE,		"Invalid Buffer Size"			},
	{ CL_INVALID_MIP_LEVEL,			"Invalid MIP Level"			},
	{ CL_INVALID_GLOBAL_WORK_SIZE,		"Invalid Global Work Size"		},
};


void
PrintCLError(cl_int errorCode, char* prefix, FILE* fp)
{
	if (errorCode == CL_SUCCESS)
		return;

	const int numErrorCodes = sizeof(ErrorCodes) / sizeof(struct errorcode);
	char* meaning = "";
	for (int i = 0; i < numErrorCodes; i++)
	{
		if (errorCode == ErrorCodes[i].statusCode)
		{
			meaning = ErrorCodes[i].meaning;
			break;
		}
	}

	fprintf(fp, "%s %s\n", prefix, meaning);
}


void
PrintOpenclInfo()
{
	cl_int status;		// returned status from opencl calls
						// test against CL_SUCCESS
	fprintf(stderr, "PrintInfo:\n");

	// find out how many platforms are attached here and get their ids:

	cl_uint numPlatforms;
	status = clGetPlatformIDs(0, NULL, &numPlatforms);
	if (status != CL_SUCCESS)
		fprintf(stderr, "clGetPlatformIDs failed (1)\n");

	fprintf(stderr, "Number of Platforms = %d\n", numPlatforms);

	cl_platform_id* platforms = new cl_platform_id[numPlatforms];
	status = clGetPlatformIDs(numPlatforms, platforms, NULL);
	if (status != CL_SUCCESS)
		fprintf(stderr, "clGetPlatformIDs failed (2)\n");

	for (int p = 0; p < (int)numPlatforms; p++)
	{
		fprintf(stderr, "Platform #%d:\n", p);
		size_t size;
		char* str;

		clGetPlatformInfo(platforms[p], CL_PLATFORM_NAME, 0, NULL, &size);
		str = new char[size];
		clGetPlatformInfo(platforms[p], CL_PLATFORM_NAME, size, str, NULL);
		fprintf(stderr, "\tName    = '%s'\n", str);
		delete[] str;

		clGetPlatformInfo(platforms[p], CL_PLATFORM_VENDOR, 0, NULL, &size);
		str = new char[size];
		clGetPlatformInfo(platforms[p], CL_PLATFORM_VENDOR, size, str, NULL);
		fprintf(stderr, "\tVendor  = '%s'\n", str);
		delete[] str;

		clGetPlatformInfo(platforms[p], CL_PLATFORM_VERSION, 0, NULL, &size);
		str = new char[size];
		clGetPlatformInfo(platforms[p], CL_PLATFORM_VERSION, size, str, NULL);
		fprintf(stderr, "\tVersion = '%s'\n", str);
		delete[] str;

		clGetPlatformInfo(platforms[p], CL_PLATFORM_PROFILE, 0, NULL, &size);
		str = new char[size];
		clGetPlatformInfo(platforms[p], CL_PLATFORM_PROFILE, size, str, NULL);
		fprintf(stderr, "\tProfile = '%s'\n", str);
		delete[] str;


		// find out how many devices are attached to each platform and get their ids:

		cl_uint numDevices;

		status = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_ALL, 0, NULL, &numDevices);
		if (status != CL_SUCCESS)
			fprintf(stderr, "clGetDeviceIDs failed (2)\n");

		fprintf(stderr, "\tNumber of Devices = %d\n", numDevices);

		cl_device_id* devices = new cl_device_id[numDevices];
		status = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_ALL, numDevices, devices, NULL);
		if (status != CL_SUCCESS)
			fprintf(stderr, "clGetDeviceIDs failed (2)\n");

		for (int d = 0; d < (int)numDevices; d++)
		{
			fprintf(stderr, "\tDevice #%d:\n", d);
			size_t size;
			cl_device_type type;
			cl_uint ui;
			size_t sizes[3] = { 0, 0, 0 };

			clGetDeviceInfo(devices[d], CL_DEVICE_TYPE, sizeof(type), &type, NULL);
			fprintf(stderr, "\t\tType = 0x%04x = ", (unsigned int)type);
			switch (type)
			{
			case CL_DEVICE_TYPE_CPU:
				fprintf(stderr, "CL_DEVICE_TYPE_CPU\n");
				break;
			case CL_DEVICE_TYPE_GPU:
				fprintf(stderr, "CL_DEVICE_TYPE_GPU\n");
				break;
			case CL_DEVICE_TYPE_ACCELERATOR:
				fprintf(stderr, "CL_DEVICE_TYPE_ACCELERATOR\n");
				break;
			default:
				fprintf(stderr, "Other...\n");
				break;
			}

			clGetDeviceInfo(devices[d], CL_DEVICE_VENDOR_ID, sizeof(ui), &ui, NULL);
			fprintf(stderr, "\t\tDevice Vendor ID = 0x%04x ", ui);
			switch (ui)
			{
			case ID_AMD:
				fprintf(stderr, "(AMD)\n");
				break;
			case ID_INTEL:
				fprintf(stderr, "(Intel)\n");
				break;
			case ID_NVIDIA:
				fprintf(stderr, "(NVIDIA)\n");
				break;
			default:
				fprintf(stderr, "(?)\n");
			}

			clGetDeviceInfo(devices[d], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(ui), &ui, NULL);
			fprintf(stderr, "\t\tDevice Maximum Compute Units = %d\n", ui);

			clGetDeviceInfo(devices[d], CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof(ui), &ui, NULL);
			fprintf(stderr, "\t\tDevice Maximum Work Item Dimensions = %d\n", ui);

			clGetDeviceInfo(devices[d], CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(sizes), sizes, NULL);
			fprintf(stderr, "\t\tDevice Maximum Work Item Sizes = %d x %d x %d\n", sizes[0], sizes[1], sizes[2]);

			clGetDeviceInfo(devices[d], CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size), &size, NULL);
			fprintf(stderr, "\t\tDevice Maximum Work Group Size = %d\n", size);

			clGetDeviceInfo(devices[d], CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(ui), &ui, NULL);
			fprintf(stderr, "\t\tDevice Maximum Clock Frequency = %d MHz\n", ui);

			size_t extensionSize;
			clGetDeviceInfo(devices[d], CL_DEVICE_EXTENSIONS, 0, NULL, &extensionSize);
			char* extensions = new char[extensionSize];
			clGetDeviceInfo(devices[d], CL_DEVICE_EXTENSIONS, extensionSize, extensions, NULL);
			fprintf(stderr, "\nDevice #%d's Extensions:\n", d);
			for (int e = 0; e < (int)strlen(extensions); e++)
			{
				if (extensions[e] == ' ')
					extensions[e] = '\n';
			}
			fprintf(stderr, "%s\n", extensions);
			delete[] extensions;
		}
		delete[] devices;
	}
	delete[] platforms;
	fprintf(stderr, "\n\n");
}

void
SelectOpenclDevice()
{
	// select which opencl device to use:
	// priority order:
	//	1. a gpu
	//	2. an nvidia or amd gpu

	int iplatform = -1;
	int idevice = -1;
	cl_device_type deviceType;
	cl_uint deviceVendor;
	cl_int status;		// returned status from opencl calls
						// test against CL_SUCCESS

	fprintf(stderr, "\nSelecting the OpenCL Platform and Device:\n");

	// find out how many platforms are attached here and get their ids:

	cl_uint numPlatforms;
	status = clGetPlatformIDs(0, NULL, &numPlatforms);
	if (status != CL_SUCCESS)
		fprintf(stderr, "clGetPlatformIDs failed (1)\n");

	cl_platform_id* platforms = new cl_platform_id[numPlatforms];
	status = clGetPlatformIDs(numPlatforms, platforms, NULL);
	if (status != CL_SUCCESS)
		fprintf(stderr, "clGetPlatformIDs failed (2)\n");

	for (int p = 0; p < (int)numPlatforms; p++)
	{
		// find out how many devices are attached to each platform and get their ids:

		cl_uint numDevices;

		status = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_ALL, 0, NULL, &numDevices);
		if (status != CL_SUCCESS)
			fprintf(stderr, "clGetDeviceIDs failed (2)\n");

		cl_device_id* devices = new cl_device_id[numDevices];
		status = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_ALL, numDevices, devices, NULL);
		if (status != CL_SUCCESS)
			fprintf(stderr, "clGetDeviceIDs failed (2)\n");

		for (int d = 0; d < 1; d++)
		{
			cl_device_type type;
			cl_uint vendor;
			size_t sizes[3] = { 0, 0, 0 };

			clGetDeviceInfo(devices[d], CL_DEVICE_TYPE, sizeof(type), &type, NULL);

			clGetDeviceInfo(devices[d], CL_DEVICE_VENDOR_ID, sizeof(vendor), &vendor, NULL);

			// select:

			if (iplatform < 0)		// not yet holding anything -- we'll accept anything
			{
				iplatform = p;
				idevice = d;
				Platform = platforms[iplatform];
				Device = devices[idevice];
				deviceType = type;
				deviceVendor = vendor;
			}
			else					// holding something already -- can we do better?
			{
				if (deviceType == CL_DEVICE_TYPE_CPU)		// holding a cpu already -- switch to a gpu if possible
				{
					if (type == CL_DEVICE_TYPE_GPU)			// found a gpu
					{										// switch to the gpu we just found
						iplatform = p;
						idevice = d;
						Platform = platforms[iplatform];
						Device = devices[idevice];
						deviceType = type;
						deviceVendor = vendor;
					}
				}
				else										// holding a gpu -- is a better gpu available?
				{
					if (deviceVendor == ID_INTEL)			// currently holding an intel gpu
					{										// we are assuming we just found a bigger, badder nvidia or amd gpu
						iplatform = p;
						idevice = d;
						Platform = platforms[iplatform];
						Device = devices[idevice];
						deviceType = type;
						deviceVendor = vendor;
					}
				}
			}
		}
	}

	if (iplatform < 0)
		fprintf(stderr, "Found no OpenCL devices!\n");
	else
		fprintf(stderr, "I have selected Platform #%d, Device #%d\n", iplatform, idevice);
}


float
Ranf(float low, float high)
{
	long random();		// returns integer 0 - TOP

	float r = (float)rand();
	return(low + r * (high - low) / (float)RAND_MAX);
}
