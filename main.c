#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stddef.h>
#include <assert.h>
#include <GL/freeglut.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include "matrix.h"

#define SPIRV

enum {
	UBLOCK_MATRIX,
};

enum {
	VATTR_VERTEX,
	VATTR_NORMAL,
	VATTR_TEXCOORD
};

struct vertex {
	float x, y, z;
	float nx, ny, nz;
	float tu, tv;
};

struct mesh {
	struct vertex *varr;
	unsigned int *iarr;
	int vcount, icount;

	unsigned int vbo, ibo, vao;
};

struct matrix_state {
	float view_mat[16];
	float proj_mat[16];
	float mvmat[16];
	float mvpmat[16];
	float lpos[3];
} __attribute__((packed));

unsigned int tex;

int init(void);
void cleanup(void);
void display(void);
void reshape(int x, int y);
void keypress(unsigned char key, int x, int y);
void mouse(int bn, int st, int x, int y);
void motion(int x, int y);

int gen_torus(struct mesh *mesh, float rad, float rrad, int usub, int vsub);
void draw_mesh(struct mesh *mesh);
unsigned int gen_texture(int width, int height);

unsigned int load_shader(const char *fname, int type);
unsigned int load_program(const char *vfname, const char *pfname);
int link_program(unsigned int prog);

void GLAPIENTRY gldebug(GLenum src, GLenum type, GLuint id, GLenum severity,
		GLsizei len, const char *msg, const void *cls);

float cam_theta, cam_phi = 25, cam_dist = 4;
int prev_x, prev_y, bnstate[8];

struct mesh torus;

unsigned int sdr;

struct matrix_state matrix_state;

unsigned int ubo_matrix;

static PFNGLSPECIALIZESHADERPROC gl_specialize_shader;

int main(int argc, char **argv)
{
	glutInit(&argc, argv);
	glutInitWindowSize(800, 600);
	glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE);
	glutInitContextProfile(GLUT_CORE_PROFILE);
	glutInitContextVersion(4, 4);
	glutCreateWindow("GL4 test");

	glutDisplayFunc(display);
	glutReshapeFunc(reshape);
	glutKeyboardFunc(keypress);
	glutMouseFunc(mouse);
	glutMotionFunc(motion);

	if(init() == -1) {
		return 1;
	}
	atexit(cleanup);

	glutMainLoop();
	return 0;
}

int init(void)
{
	glDebugMessageCallback(gldebug, 0);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	gl_specialize_shader = (PFNGLSPECIALIZESHADERPROC)glXGetProcAddress((unsigned char*)"glSpecializeShaderARB");
	if(!gl_specialize_shader) {
		fprintf(stderr, "failed to load glSpecializeShaderARB entry point\n");
		return -1;
	}

	if(!(tex = gen_texture(256, 256))) {
		return -1;
	}

	if(gen_torus(&torus, 1.0, 0.25, 32, 12) == -1) {
		return -1;
	}

#ifndef SPIRV
	if(!(sdr = load_program("vertex.glsl", "pixel.glsl"))) {
		return -1;
	}
#else
	if(!(sdr = load_program("spirv/vertex.spv", "spirv/pixel.spv"))) {
		return -1;
	}
#endif

	glUseProgram(sdr);

	glGenBuffers(1, &ubo_matrix);
	glBindBuffer(GL_UNIFORM_BUFFER, ubo_matrix);
	glBufferData(GL_UNIFORM_BUFFER, sizeof matrix_state, &matrix_state, GL_STREAM_DRAW);

	return 0;
}

void cleanup(void)
{
	free(torus.varr);
	free(torus.iarr);
	if(torus.vbo) {
		glDeleteBuffers(1, &torus.vbo);
		glDeleteBuffers(1, &torus.ibo);
	}
	if(torus.vao) {
		glDeleteVertexArrays(1, &torus.vao);
	}
	glDeleteTextures(1, &tex);
	glDeleteBuffers(1, &ubo_matrix);
}

void display(void)
{
	matrix_state.lpos[0] = -10;
	matrix_state.lpos[1] = 10;
	matrix_state.lpos[2] = 10;

	glClearColor(0.05, 0.05, 0.05, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	mat_identity(matrix_state.view_mat);
	mat_translate(matrix_state.view_mat, 0, 0, -cam_dist);
	mat_rotate(matrix_state.view_mat, cam_phi, 1, 0, 0);
	mat_rotate(matrix_state.view_mat, cam_theta, 0, 1, 0);

	mat_copy(matrix_state.mvmat, matrix_state.view_mat);

	mat_copy(matrix_state.mvpmat, matrix_state.proj_mat);
	mat_mul(matrix_state.mvpmat, matrix_state.mvmat);

	mat_transform(matrix_state.view_mat, matrix_state.lpos);

	glUseProgram(sdr);

	glBindBuffer(GL_UNIFORM_BUFFER, ubo_matrix);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof matrix_state, &matrix_state);
	glBindBufferBase(GL_UNIFORM_BUFFER, UBLOCK_MATRIX, ubo_matrix);

	glBindTexture(GL_TEXTURE_2D, tex);
	draw_mesh(&torus);

	assert(glGetError() == GL_NO_ERROR);
	glutSwapBuffers();
}

void reshape(int x, int y)
{
	glViewport(0, 0, x, y);

	mat_identity(matrix_state.proj_mat);
	mat_perspective(matrix_state.proj_mat, 50.0, (float)x / (float)y, 0.5, 500.0);
}

void keypress(unsigned char key, int x, int y)
{
	switch(key) {
	case 27:
		exit(0);
	}
}

void mouse(int bn, int st, int x, int y)
{
	bnstate[bn - GLUT_LEFT_BUTTON] = st == GLUT_DOWN ? 1 : 0;
	prev_x = x;
	prev_y = y;
}

void motion(int x, int y)
{
	int dx = x - prev_x;
	int dy = y - prev_y;
	prev_x = x;
	prev_y = y;

	if(!dx && !dy) return;

	if(bnstate[0]) {
		cam_theta += dx * 0.5;
		cam_phi += dy * 0.5;
		if(cam_phi < -90) cam_phi = -90;
		if(cam_phi > 90) cam_phi = 90;
		glutPostRedisplay();
	}
	if(bnstate[2]) {
		cam_dist += dy * 0.1;
		if(cam_dist < 0.0) cam_dist = 0.0;
		glutPostRedisplay();
	}
}

static void torus_vertex(struct vertex *vout, float rad, float rrad, float u, float v)
{
	float theta = u * M_PI * 2.0;
	float phi = v * M_PI * 2.0;
	float rx, ry, rz, cx, cy, cz;

	cx = sin(theta) * rad;
	cy = 0.0;
	cz = -cos(theta) * rad;

	rx = -cos(phi) * rrad + rad;
	ry = sin(phi) * rrad;
	rz = 0.0;

	vout->x = rx * sin(theta) + rz * cos(theta);
	vout->y = ry;
	vout->z = -rx * cos(theta) + rz * sin(theta);

	vout->nx = (vout->x - cx) / rrad;
	vout->ny = (vout->y - cy) / rrad;
	vout->nz = (vout->z - cz) / rrad;

	vout->tu = u;
	vout->tv = v;
}


int gen_torus(struct mesh *mesh, float rad, float rrad, int usub, int vsub)
{
	int i, j, uverts, vverts, nverts, nquads, ntri;
	float u, v;
	float du = 1.0 / (float)usub;
	float dv = 1.0 / (float)vsub;
	struct vertex *vptr;
	unsigned int *iptr;

	if(usub < 3) usub = 3;
	if(vsub < 3) vsub = 3;

	uverts = usub + 1;
	vverts = vsub + 1;

	nverts = uverts * vverts;
	nquads = usub * vsub;
	ntri = nquads * 2;

	mesh->vcount = nverts;
	mesh->icount = ntri * 3;

	if(!(mesh->varr = malloc(mesh->vcount * sizeof *mesh->varr))) {
		fprintf(stderr, "failed to allocate vertex array for %d vertices\n", mesh->vcount);
		return -1;
	}
	vptr = mesh->varr;
	if(!(mesh->iarr = malloc(mesh->icount * sizeof *mesh->iarr))) {
		fprintf(stderr, "failed to allocate index array for %d indices\n", mesh->icount);
		free(mesh->varr);
		mesh->varr = 0;
		return -1;
	}
	iptr = mesh->iarr;

	u = 0.0;
	for(i=0; i<uverts; i++) {
		v = 0.0;
		for(j=0; j<vverts; j++) {
			torus_vertex(vptr++, rad, rrad, u, v);

			if(i < usub && j < vsub) {
				int vnum = i * vverts + j;
				*iptr++ = vnum;
				*iptr++ = vnum + vverts + 1;
				*iptr++ = vnum + 1;
				*iptr++ = vnum;
				*iptr++ = vnum + vverts;
				*iptr++ = vnum + vverts + 1;
			}

			v += dv;
		}
		u += du;
	}

	glGenBuffers(1, &mesh->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
	glBufferData(GL_ARRAY_BUFFER, mesh->vcount * sizeof *mesh->varr, mesh->varr, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glGenBuffers(1, &mesh->ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh->icount * sizeof *mesh->iarr, mesh->iarr, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	glGenVertexArrays(1, &mesh->vao);
	glBindVertexArray(mesh->vao);

	glEnableVertexAttribArray(VATTR_VERTEX);
	glEnableVertexAttribArray(VATTR_NORMAL);
	glEnableVertexAttribArray(VATTR_TEXCOORD);

	glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
	glVertexAttribPointer(VATTR_VERTEX, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex), 0);
	glVertexAttribPointer(VATTR_NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (void*)offsetof(struct vertex, nx));
	glVertexAttribPointer(VATTR_TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (void*)offsetof(struct vertex, tu));
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glBindVertexArray(0);
	return 0;
}

void draw_mesh(struct mesh *mesh)
{
	glBindVertexArray(mesh->vao);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ibo);
	glDrawElements(GL_TRIANGLES, mesh->icount, GL_UNSIGNED_INT, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	glBindVertexArray(0);
}

unsigned int gen_texture(int width, int height)
{
	int i, j;
	unsigned int tex;
	unsigned char *pixels, *ptr;

	if(!(pixels = malloc(width * height * 3))) {
		return 0;
	}
	ptr = pixels;

	for(i=0; i<height; i++) {
		for(j=0; j<width; j++) {
			int x = i ^ j;
			*ptr++ = x;
			*ptr++ = (x << 1) & 0xff;
			*ptr++ = (x << 2) & 0xff;
		}
	}

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
	glGenerateMipmap(GL_TEXTURE_2D);

	free(pixels);
	return tex;
}

unsigned int load_shader(const char *fname, int type)
{
	unsigned int sdr;
	int fsz;
	char *buf;
	FILE *fp;
	int status, loglen;

	if(!(fp = fopen(fname, "rb"))) {
		fprintf(stderr, "failed to open shader: %s\n", fname);
		return 0;
	}
	fseek(fp, 0, SEEK_END);
	fsz = ftell(fp);
	rewind(fp);

	if(!(buf = malloc(fsz + 1))) {
		fprintf(stderr, "failed to allocate %d bytes\n", fsz + 1);
		fclose(fp);
		return 0;
	}
	if(fread(buf, 1, fsz, fp) < fsz) {
		fprintf(stderr, "failed to read shader: %s\n", fname);
		free(buf);
		fclose(fp);
		return 0;
	}
	buf[fsz] = 0;
	fclose(fp);

	sdr = glCreateShader(type);

#ifndef SPIRV
	glShaderSource(sdr, 1, (const char**)&buf, 0);
	glCompileShader(sdr);
#else
	glShaderBinary(1, &sdr, GL_SHADER_BINARY_FORMAT_SPIR_V_ARB, buf, fsz);
	gl_specialize_shader(sdr, "main", 0, 0, 0);
#endif
	free(buf);

	glGetShaderiv(sdr, GL_COMPILE_STATUS, &status);
	if(status) {
		printf("successfully compiled shader: %s\n", fname);
	} else {
		printf("failed to compile shader: %s\n", fname);
	}

	glGetShaderiv(sdr, GL_INFO_LOG_LENGTH, &loglen);
	if(loglen > 0 && (buf = malloc(loglen + 1))) {
		glGetShaderInfoLog(sdr, loglen, 0, buf);
		buf[loglen] = 0;
		printf("%s\n", buf);
		free(buf);
	}

	if(!status) {
		glDeleteShader(sdr);
		return 0;
	}
	return sdr;
}

unsigned int load_program(const char *vfname, const char *pfname)
{
	unsigned int vs, ps, prog;

	if(!(vs = load_shader(vfname, GL_VERTEX_SHADER))) {
		return 0;
	}
	if(!(ps = load_shader(pfname, GL_FRAGMENT_SHADER))) {
		glDeleteShader(vs);
		return 0;
	}

	prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, ps);

	if(link_program(prog) == -1) {
		glDeleteShader(vs);
		glDeleteShader(ps);
		glDeleteProgram(prog);
		return 0;
	}

	glDetachShader(prog, vs);
	glDetachShader(prog, ps);

	return prog;
}

int link_program(unsigned int prog)
{
	int status, loglen;
	char *buf;

	glLinkProgram(prog);

	glGetProgramiv(prog, GL_LINK_STATUS, &status);
	if(status) {
		printf("successfully linked shader program\n");
	} else {
		printf("failed to link shader program\n");
	}

	glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &loglen);
	if(loglen > 0 && (buf = malloc(loglen + 1))) {
		glGetProgramInfoLog(prog, loglen, 0, buf);
		buf[loglen] = 0;
		printf("%s\n", buf);
		free(buf);
	}

	return status ? 0 : -1;
}

const char *gldebug_srcstr(unsigned int src)
{
	switch(src) {
	case GL_DEBUG_SOURCE_API:
		return "api";
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
		return "wsys";
	case GL_DEBUG_SOURCE_SHADER_COMPILER:
		return "sdrc";
	case GL_DEBUG_SOURCE_THIRD_PARTY:
		return "3rdparty";
	case GL_DEBUG_SOURCE_APPLICATION:
		return "app";
	case GL_DEBUG_SOURCE_OTHER:
		return "other";
	default:
		break;
	}
	return "unknown";
}

const char *gldebug_typestr(unsigned int type)
{
	switch(type) {
	case GL_DEBUG_TYPE_ERROR:
		return "error";
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
		return "deprecated";
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
		return "undefined behavior";
	case GL_DEBUG_TYPE_PORTABILITY:
		return "portability";
	case GL_DEBUG_TYPE_PERFORMANCE:
		return "performance";
	case GL_DEBUG_TYPE_OTHER:
		return "other";
	default:
		break;
	}
	return "unknown";
}

void GLAPIENTRY gldebug(GLenum src, GLenum type, GLuint id, GLenum severity,
		GLsizei len, const char *msg, const void *cls)
{
	printf("[GLDEBUG] (%s) %s: %s\n", gldebug_srcstr(src), gldebug_typestr(type), msg);
}

