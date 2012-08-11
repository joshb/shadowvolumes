/*
 * Copyright (C) 2003-2004 Josh A. Beam
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <math.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glut.h>

#define M_INFINITY 50.0f
#define LIGHTMAP_SIZE 16

#ifndef glActiveTextureARB
extern void glActiveTextureARB(GLenum);
#endif
#ifndef glMultiTexCoord2fARB
extern void glMultiTexCoord2fARB(GLenum, float, float);
#endif

extern unsigned char *read_pcx(const char *filename, unsigned int *widthp, unsigned int *heightp);

struct surface {
	float vertices[4][3];
	float matrix[9];

	float s_dist, t_dist;
};

struct cube {
	struct surface *surfaces[6];
	float position[3];
};

static float
dot_product(float v1[3], float v2[3])
{
	return (v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2]);
}

static void
normalize(float v[3])
{
	float f = 1.0f / sqrt(dot_product(v, v));

	v[0] *= f;
	v[1] *= f;
	v[2] *= f;
}

static void
cross_product(const float *v1, const float *v2, float *out)
{
	out[0] = v1[1] * v2[2] - v1[2] * v2[1];
	out[1] = v1[2] * v2[0] - v1[0] * v2[2];
	out[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

static void
multiply_vector_by_matrix(const float m[9], float v[3])
{
	float tmp[3];

	tmp[0] = v[0] * m[0] + v[1] * m[3] + v[2] * m[6];
	tmp[1] = v[0] * m[1] + v[1] * m[4] + v[2] * m[7];
	tmp[2] = v[0] * m[2] + v[1] * m[5] + v[2] * m[8];

	v[0] = tmp[0];
	v[1] = tmp[1];
	v[2] = tmp[2];
}

static struct surface *
new_surface(float vertices[4][3])
{
	int i, j;
	struct surface *surf;

	surf = malloc(sizeof(struct surface));
	if(!surf) {
		fprintf(stderr, "Error: Couldn't allocate memory for surface\n");
		return NULL;
	}

	for(i = 0; i < 4; i++) {
		for(j = 0; j < 3; j++)
			surf->vertices[i][j] = vertices[i][j];
	}

	/* x axis of matrix points in world space direction of the s texture axis */
	for(i = 0; i < 3; i++)
		surf->matrix[0 + i] = surf->vertices[3][i] - surf->vertices[0][i];
	surf->s_dist = sqrt(dot_product(surf->matrix, surf->matrix));
	normalize(surf->matrix);

	/* y axis of matrix points in world space direction of the t texture axis */
	for(i = 0; i < 3; i++)
		surf->matrix[3 + i] = surf->vertices[1][i] - surf->vertices[0][i];
	surf->t_dist = sqrt(dot_product(surf->matrix + 3, surf->matrix + 3));
	normalize(surf->matrix + 3);

	/* z axis of matrix is the surface's normal */
	cross_product(surf->matrix, surf->matrix + 3, surf->matrix + 6);

	return surf;
}

static void
render_surface_shadow_volume(struct surface *surf,
                             float *surf_pos, float *light_pos)
{
	int i;
	float v[4][3];

	for(i = 0; i < 4; i++) {
		surf->vertices[i][0] += surf_pos[0];
		surf->vertices[i][1] += surf_pos[1];
		surf->vertices[i][2] += surf_pos[2];

		v[i][0] = (surf->vertices[i][0] - light_pos[0]);
		v[i][1] = (surf->vertices[i][1] - light_pos[1]);
		v[i][2] = (surf->vertices[i][2] - light_pos[2]);
		normalize(v[i]);
		v[i][0] *= M_INFINITY;
		v[i][1] *= M_INFINITY;
		v[i][2] *= M_INFINITY;
		v[i][0] += light_pos[0];
		v[i][1] += light_pos[1];
		v[i][2] += light_pos[2];
	}

	/* back cap */
	glBegin(GL_QUADS);
		glVertex3fv(v[3]);
		glVertex3fv(v[2]);
		glVertex3fv(v[1]);
		glVertex3fv(v[0]);
	glEnd();

	/* front cap */
	glBegin(GL_QUADS);
		glVertex3fv(surf->vertices[0]);
		glVertex3fv(surf->vertices[1]);
		glVertex3fv(surf->vertices[2]);
		glVertex3fv(surf->vertices[3]);
	glEnd();

	glBegin(GL_QUAD_STRIP);
	glVertex3fv(surf->vertices[0]);
	glVertex3fv(v[0]);
	for(i = 1; i <= 4; i++) {
		glVertex3fv(surf->vertices[i % 4]);
		glVertex3fv(v[i % 4]);
	}
	glEnd();

	for(i = 0; i < 4; i++) {
		surf->vertices[i][0] -= surf_pos[0];
		surf->vertices[i][1] -= surf_pos[1];
		surf->vertices[i][2] -= surf_pos[2];
	}
}

static struct cube *
new_cube(float size)
{
	int i;
	struct cube *c;
	float v[4][3];
	int error = 0;

	c = malloc(sizeof(struct cube));
	if(!c)
		return NULL;

	c->position[0] = 0.0f;
	c->position[1] = 0.0f;
	c->position[2] = 0.0f;

	v[0][0] = -size; v[0][1] = size; v[0][2] = size;
	v[1][0] = -size; v[1][1] = -size; v[1][2] = size;
	v[2][0] = size; v[2][1] = -size; v[2][2] = size;
	v[3][0] = size; v[3][1] = size; v[3][2] = size;
	c->surfaces[0] = new_surface(v);

	v[0][0] = size; v[0][1] = size; v[0][2] = -size;
	v[1][0] = size; v[1][1] = -size; v[1][2] = -size;
	v[2][0] = -size; v[2][1] = -size; v[2][2] = -size;
	v[3][0] = -size; v[3][1] = size; v[3][2] = -size;
	c->surfaces[1] = new_surface(v);

	v[0][0] = -size; v[0][1] = size; v[0][2] = -size;
	v[1][0] = -size; v[1][1] = size; v[1][2] = size;
	v[2][0] = size; v[2][1] = size; v[2][2] = size;
	v[3][0] = size; v[3][1] = size; v[3][2] = -size;
	c->surfaces[2] = new_surface(v);

	v[0][0] = size; v[0][1] = -size; v[0][2] = -size;
	v[1][0] = size; v[1][1] = -size; v[1][2] = size;
	v[2][0] = -size; v[2][1] = -size; v[2][2] = size;
	v[3][0] = -size; v[3][1] = -size; v[3][2] = -size;
	c->surfaces[3] = new_surface(v);

	v[0][0] = -size; v[0][1] = size; v[0][2] = size;
	v[1][0] = -size; v[1][1] = size; v[1][2] = -size;
	v[2][0] = -size; v[2][1] = -size; v[2][2] = -size;
	v[3][0] = -size; v[3][1] = -size; v[3][2] = size;
	c->surfaces[4] = new_surface(v);

	v[0][0] = size; v[0][1] = -size; v[0][2] = size;
	v[1][0] = size; v[1][1] = -size; v[1][2] = -size;
	v[2][0] = size; v[2][1] = size; v[2][2] = -size;
	v[3][0] = size; v[3][1] = size; v[3][2] = size;
	c->surfaces[5] = new_surface(v);

	for(i = 0; i < 6; i++) {
		if(!c->surfaces[i])
			error = 1;
	}

	if(error != 0) {
		for(i = 0; i < 6; i++) {
			if(c->surfaces[i])
				free(c->surfaces[i]);
		}

		free(c);
		return NULL;
	}

	return c;
}

static float light_pos[3] = { 10.0f, 0.0f, 8.0f };
static float light_color[3] = { 1.0f, 1.0f, 1.0f };

static unsigned int
generate_lightmap(struct surface *surf, float *position)
{
	static unsigned char data[LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3];
	static unsigned int lightmap_tex_num = 0;
	unsigned int i, j;
	float pos[3];
	float step, s, t;
	float light_scale = 0.1f;
	float d_scale = 0.005f;

	if(lightmap_tex_num == 0)
		glGenTextures(1, &lightmap_tex_num);

	step = 1.0f / (float)LIGHTMAP_SIZE;

	s = t = 0.0f;
	for(i = 0; i < LIGHTMAP_SIZE; i++) {
		for(j = 0; j < LIGHTMAP_SIZE; j++) {
			float d;
			float tmp;

			pos[0] = surf->s_dist * s;
			pos[1] = surf->t_dist * t;
			pos[2] = 0.0f;
			multiply_vector_by_matrix(surf->matrix, pos);

			pos[0] += position[0] + surf->vertices[0][0];
			pos[1] += position[1] + surf->vertices[0][1];
			pos[2] += position[2] + surf->vertices[0][2];

			pos[0] -= light_pos[0] * light_scale;
			pos[1] -= light_pos[1] * light_scale;
			pos[2] -= light_pos[2] * light_scale;

			d = dot_product(pos, pos) * d_scale;
			if(d < 1.0f)
				d = 1.0f;
			tmp = 1.0f / d;

			data[i * LIGHTMAP_SIZE * 3 + j * 3 + 0] = (unsigned char)(255.0f * tmp * light_color[0]);
			data[i * LIGHTMAP_SIZE * 3 + j * 3 + 1] = (unsigned char)(255.0f * tmp * light_color[1]);
			data[i * LIGHTMAP_SIZE * 3 + j * 3 + 2] = (unsigned char)(255.0f * tmp * light_color[2]);

			s += step;
		}

		t += step;
		s = 0.0f;
	}

	glBindTexture(GL_TEXTURE_2D, lightmap_tex_num);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexImage2D(GL_TEXTURE_2D, 0, 3, LIGHTMAP_SIZE, LIGHTMAP_SIZE, 0, GL_RGB, GL_UNSIGNED_BYTE, data);

	return lightmap_tex_num;
}

static int lighting = 1;

void
scene_toggle_lighting()
{
	lighting = lighting ? 0 : 1;
}

static void
draw_shadow()
{
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, 1, 1, 0, 0, 1);
	glDisable(GL_DEPTH_TEST);

	glColor4f(0.0f, 0.0f, 0.0f, 0.5f);
	glBegin(GL_QUADS);
		glVertex2i(0, 0);
		glVertex2i(0, 1);
		glVertex2i(1, 1);
		glVertex2i(1, 0);
	glEnd();

	glEnable(GL_DEPTH_TEST);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

static void
render_surface(struct surface *surf, float *position)
{
	glPushMatrix();
	glTranslatef(position[0], position[1], position[2]);

	if(lighting)
		glBindTexture(GL_TEXTURE_2D, generate_lightmap(surf, position));
	glBegin(GL_QUADS);
		glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0f, 0.0f);
		glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.0f, 0.0f);
		glVertex3fv(surf->vertices[0]);
		glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 0.0f, 1.0f);
		glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.0f, 1.0f);
		glVertex3fv(surf->vertices[1]);
		glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 1.0f, 1.0f);
		glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 1.0f, 1.0f);
		glVertex3fv(surf->vertices[2]);
		glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 1.0f, 0.0f);
		glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 1.0f, 0.0f);
		glVertex3fv(surf->vertices[3]);
	glEnd();

	glPopMatrix();
}

static void
render_cube(struct cube *c)
{
	int i;

	for(i = 0; i < 6; i++)
		render_surface(c->surfaces[i], c->position);
}

static void
render_cube_shadow(struct cube *c)
{
	int i;

	for(i = 0; i < 6; i++) {
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glDepthMask(GL_FALSE);
		glEnable(GL_CULL_FACE);
		glEnable(GL_STENCIL_TEST);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(0.0f, 100.0f);

		glCullFace(GL_FRONT);
		glStencilFunc(GL_ALWAYS, 0x0, 0xff);
		glStencilOp(GL_KEEP, GL_INCR, GL_KEEP);
		render_surface_shadow_volume(c->surfaces[i], c->position, light_pos);

		glCullFace(GL_BACK);
		glStencilFunc(GL_ALWAYS, 0x0, 0xff);
		glStencilOp(GL_KEEP, GL_DECR, GL_KEEP);
		render_surface_shadow_volume(c->surfaces[i], c->position, light_pos);

		glDisable(GL_POLYGON_OFFSET_FILL);
		glDisable(GL_CULL_FACE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glDepthMask(GL_TRUE);

		glStencilFunc(GL_NOTEQUAL, 0x0, 0xff);
		glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
		draw_shadow();
		glDisable(GL_STENCIL_TEST);
	}
}

static float cam_rot[3] = { 22.0f, 0.0f, 0.0f };
static struct cube *cubes[4];
static float sphere_pos[3] = { -10.0f, -5.0f, -10.0f };

void
scene_render()
{
	static struct cube *room;
	static unsigned int surface_tex_num;
	static GLUquadricObj *sphere;
	int i;

	if(!cubes[0]) {
		unsigned char *data;
		unsigned int width, height;

		glEnable(GL_TEXTURE_2D);

		/* load texture */
		data = read_pcx("texture.pcx", &width, &height);
		glEnable(GL_TEXTURE_2D);
		glGenTextures(1, &surface_tex_num);
		glBindTexture(GL_TEXTURE_2D, surface_tex_num);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glTexImage2D(GL_TEXTURE_2D, 0, 3, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);

		/* create objects */
		room = new_cube(15.0f);

		cubes[0] = new_cube(1.0f);
		cubes[0]->position[0] = -2.0f;
		cubes[0]->position[1] = -2.0f;
		cubes[1] = new_cube(1.0f);
		cubes[1]->position[0] = 2.0f;
		cubes[1]->position[1] = -2.0f;
		cubes[2] = new_cube(1.0f);
		cubes[2]->position[0] = 2.0f;
		cubes[2]->position[1] = 2.0f;
		cubes[3] = new_cube(1.0f);
		cubes[3]->position[0] = -2.0f;
		cubes[3]->position[1] = 2.0f;

		sphere = gluNewQuadric();
	}

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	glLoadIdentity();
	glTranslatef(0.0f, 0.0f, -15.0f);
	glRotatef(cam_rot[0], 1.0f, 0.0f, 0.0f);
	glRotatef(cam_rot[1], 0.0f, 1.0f, 0.0f);
	glRotatef(cam_rot[2], 0.0f, 0.0f, 1.0f);

	glColor4f(0.75f, 0.0f, 0.0f, 1.0f);
	glPushMatrix();
	glTranslatef(sphere_pos[0], sphere_pos[1], sphere_pos[2]);
	gluSphere(sphere, 2.5f, 30, 30);
	glPopMatrix();

	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	glActiveTextureARB(GL_TEXTURE0_ARB);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, surface_tex_num);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	if(lighting)
		glEnable(GL_TEXTURE_2D);

	/* render each cube */
	for(i = 0; i < 4; i++) {
		if(!cubes[i])
			break;

		render_cube(cubes[i]);
	}

	render_cube(room);

	glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glDisable(GL_TEXTURE_2D);

	/* render shadows */
	for(i = 0; i < 4; i++)
		render_cube_shadow(cubes[i]);

	/* render light */
	glColor3fv(light_color);
	glBegin(GL_QUADS);
		glVertex3f(light_pos[0] - 0.1f, light_pos[1] + 0.1f, light_pos[2] + 0.1f);
		glVertex3f(light_pos[0] - 0.1f, light_pos[1] - 0.1f, light_pos[2] + 0.1f);
		glVertex3f(light_pos[0] + 0.1f, light_pos[1] - 0.1f, light_pos[2] + 0.1f);
		glVertex3f(light_pos[0] + 0.1f, light_pos[1] + 0.1f, light_pos[2] + 0.1f);
	glEnd();
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	glFlush();
	glutSwapBuffers();
}

static unsigned int
get_ticks()
{
	struct timeval t;

	gettimeofday(&t, NULL);

	return (t.tv_sec * 1000) + (t.tv_usec / 1000);
}

void
scene_cycle()
{
	static float light_rot = 0.0f;
	static float sphere_rot = 0.0f;
	static unsigned int prev_ticks = 0;
	unsigned int ticks;
	float time;

	if(!prev_ticks)
		prev_ticks = get_ticks();

	ticks = get_ticks();
	time = (float)(ticks - prev_ticks);
	prev_ticks = ticks;

	light_pos[0] = cosf(light_rot) * 5.0f;
	light_rot += 0.0005f * time;

	sphere_pos[0] = cosf(sphere_rot) * 10.0f;
	sphere_pos[1] = sinf(sphere_rot) * 5.0f;
	sphere_rot += 0.0001f * time;

	scene_render();
}
