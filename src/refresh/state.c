/*
Copyright (C) 2003-2006 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "gl.h"

glState_t gls;

const mat4_t gl_identity = { [0] = 1, [5] = 1, [10] = 1, [15] = 1 };

// for uploading
void GL_ForceTexture(glTmu_t tmu, GLuint texnum)
{
    GL_ActiveTexture(tmu);

    Q_assert(tmu < MAX_TMUS);
    if (gls.texnums[tmu] == texnum)
        return;

    qglBindTexture(GL_TEXTURE_2D, texnum);
    gls.texnums[tmu] = texnum;

    c.texSwitches++;
}

// for drawing
void GL_BindTexture(glTmu_t tmu, GLuint texnum)
{
#if USE_DEBUG
    if (gl_nobind->integer && tmu == TMU_TEXTURE)
        texnum = TEXNUM_DEFAULT;
#endif

    if (glr.shadowbuffer_bound)
        return;

    Q_assert(tmu < MAX_TMUS);
    if (gls.texnums[tmu] == texnum)
        return;

    if (qglBindTextureUnit) {
        qglBindTextureUnit(tmu, texnum);
    } else {
        GL_ActiveTexture(tmu);
        qglBindTexture(GL_TEXTURE_2D, texnum);
    }
    gls.texnums[tmu] = texnum;

    c.texSwitches++;
}

void GL_ForceCubemap(GLuint texnum)
{
    GL_ActiveTexture(TMU_TEXTURE);

    if (gls.texnumcube == texnum)
        return;

    qglBindTexture(GL_TEXTURE_CUBE_MAP, texnum);
    gls.texnumcube = texnum;

    c.texSwitches++;
}

void GL_BindCubemap(GLuint texnum)
{
    if (!gl_drawsky->integer)
        texnum = TEXNUM_CUBEMAP_BLACK;

    if (gls.texnumcube == texnum)
        return;

    if (qglBindTextureUnit) {
        qglBindTextureUnit(TMU_TEXTURE, texnum);
    } else {
        GL_ActiveTexture(TMU_TEXTURE);
        qglBindTexture(GL_TEXTURE_CUBE_MAP, texnum);
    }
    gls.texnumcube = texnum;

    c.texSwitches++;
}

void GL_ForceTextureArray(GLuint texnum)
{
    GL_ActiveTexture(TMU_LIGHTMAP);

    if (gls.texnumarray == texnum)
        return;

    qglBindTexture(GL_TEXTURE_2D_ARRAY, texnum);
    gls.texnumarray = texnum;

    c.texSwitches++;
}

void GL_BindTextureArray(GLuint texnum)
{
    if (gls.texnumarray == texnum)
        return;

    if (qglBindTextureUnit) {
        qglBindTextureUnit(TMU_LIGHTMAP, texnum);
    } else {
        GL_ActiveTexture(TMU_LIGHTMAP);
        qglBindTexture(GL_TEXTURE_2D_ARRAY, texnum);
    }
    gls.texnumarray = texnum;

    c.texSwitches++;
}

void GL_DeleteBuffers(GLsizei n, const GLuint *buffers)
{
    qglDeleteBuffers(n, buffers);

    // invalidate bindings
    for (int i = 0; i < n; i++)
        for (int j = 0; j < GLB_COUNT; j++)
            if (gls.currentbuffer[j] == buffers[i])
                gls.currentbuffer[j] = 0;
}

static void GL_ScrollPos(vec2_t scroll, glStateBits_t bits)
{
    float speed = 1.6f;

    if (bits & (GLS_SCROLL_X | GLS_SCROLL_Y))
        speed = 0.78125f;
    else if (bits & GLS_SCROLL_SLOW)
        speed = 0.5f;

    if (bits & GLS_SCROLL_FLIP)
        speed = -speed;

    speed *= glr.fd.time;

    if (bits & GLS_SCROLL_Y) {
        scroll[0] = 0;
        scroll[1] = speed;
    } else {
        scroll[0] = -speed;
        scroll[1] = 0;
    }
}

void GL_StateBits(glStateBits_t bits)
{
    glStateBits_t diff = bits ^ gls.state_bits;

    if (!diff)
        return;

    if (diff & GLS_BLEND_MASK) {
        if (bits & GLS_BLEND_MASK) {
            qglEnable(GL_BLEND);
            if (bits & GLS_BLEND_BLEND)
                qglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            else if (bits & GLS_BLEND_ADD)
                qglBlendFunc(GL_SRC_ALPHA, GL_ONE);
            else if (bits & GLS_BLEND_MODULATE)
                qglBlendFunc(GL_DST_COLOR, GL_ONE);
        } else {
            qglDisable(GL_BLEND);
        }
    }

    if (diff & GLS_DEPTHMASK_FALSE) {
        if (bits & GLS_DEPTHMASK_FALSE)
            qglDepthMask(GL_FALSE);
        else
            qglDepthMask(GL_TRUE);
    }

    if (diff & GLS_DEPTHTEST_DISABLE) {
        if (bits & GLS_DEPTHTEST_DISABLE)
            qglDisable(GL_DEPTH_TEST);
        else
            qglEnable(GL_DEPTH_TEST);
    }

    if (diff & GLS_CULL_DISABLE) {
        if (bits & GLS_CULL_DISABLE)
            qglDisable(GL_CULL_FACE);
        else
            qglEnable(GL_CULL_FACE);
    }

    if (diff & GLS_SHADER_MASK)
        GL_ShaderStateBits(bits & GLS_SHADER_MASK);

    if (diff & GLS_SCROLL_MASK && bits & GLS_SCROLL_ENABLE) {
        GL_ScrollPos(gls.u_block.scroll, bits);
        gls.u_block_dirty |= DIRTY_BLOCK;
    }

    if (diff & GLS_BLOOM_GENERATE && glr.framebuffer_bound) {
        int n = (bits & GLS_BLOOM_GENERATE) ? 2 : 1;
        qglDrawBuffers(n, (const GLenum []) {
            GL_COLOR_ATTACHMENT0,
            GL_COLOR_ATTACHMENT1
        });
    }

    gls.state_bits = bits;
}

void GL_ArrayBits(glArrayBits_t bits)
{
    glArrayBits_t diff = bits ^ gls.array_bits;

    if (!diff)
        return;

    for (int i = 0; i < VERT_ATTR_COUNT; i++) {
        if (!(diff & BIT(i)))
            continue;
        if (bits & BIT(i))
            qglEnableVertexAttribArray(i);
        else
            qglDisableVertexAttribArray(i);
    }

    gls.array_bits = bits;
}

void GL_Ortho(GLfloat xmin, GLfloat xmax, GLfloat ymin, GLfloat ymax, GLfloat znear, GLfloat zfar)
{
    Matrix_Ortho(xmin, xmax, ymin, ymax, znear, zfar, gls.proj_matrix);
    gls.u_block_dirty |= DIRTY_MATRIX;
}

void GL_Setup2D(void)
{
    qglViewport(0, 0, r_config.width, r_config.height);

    GL_Ortho(0, r_config.width, r_config.height, 0, -1, 1);
    draw.scale = 1;

    draw.colors[0].u32 = U32_WHITE;
    draw.colors[1].u32 = U32_WHITE;

    if (draw.scissor) {
        qglDisable(GL_SCISSOR_TEST);
        draw.scissor = false;
    }

    gls.u_block.time = glr.fd.time;
    gls.u_block.modulate_world = 1.0f;
    gls.u_block.modulate_entities = 1.0f;

    gls.u_block.w_amp[0] = 0.0025f;
    gls.u_block.w_amp[1] = 0.0025f;
    gls.u_block.w_phase[0] = M_PIf * 10;
    gls.u_block.w_phase[1] = M_PIf * 10;

    GL_ForceMatrix(gl_identity);
}

void GL_Frustum(GLfloat fov_x, GLfloat fov_y, GLfloat reflect_x)
{
    GLfloat zfar;

    if (glr.fd.rdflags & RDF_NOWORLDMODEL)
        zfar = 2048;
    else
        zfar = gl_static.world.size * 2;

    Matrix_Frustum(fov_x, fov_y, gl_znear->value, zfar, gls.proj_matrix);
    gls.proj_matrix[0] *= reflect_x;

    gls.u_block_dirty |= DIRTY_MATRIX;
}

void GL_RotateForViewer(void)
{
    AnglesToAxis(glr.fd.viewangles, glr.viewaxis);
    Matrix_RotateForViewer(glr.fd.vieworg, glr.viewaxis, glr.viewmatrix);
    GL_ForceMatrix(glr.viewmatrix);
}

void GL_Setup3D(void)
{
    if (glr.framebuffer_bound)
        qglViewport(0, 0, glr.fd.width, glr.fd.height);
    else
        qglViewport(glr.fd.x, r_config.height - (glr.fd.y + glr.fd.height),
                    glr.fd.width, glr.fd.height);

    gls.u_block.time = glr.fd.time;
    gls.u_block.modulate_world = gl_modulate->value * gl_modulate_world->value;
    gls.u_block.modulate_entities = gl_modulate->value * gl_modulate_entities->value;

    gls.u_block.w_amp[0] = 0.0625f;
    gls.u_block.w_amp[1] = 0.0625f;
    gls.u_block.w_phase[0] = 4;
    gls.u_block.w_phase[1] = 4;

    R_RotateForSky();

    gls.light_bits = 0;

    GL_BindBuffer(GL_UNIFORM_BUFFER, gl_static.uniform_buffers[UBO_STYLES]);
    qglBufferData(GL_UNIFORM_BUFFER, sizeof(gls.u_styles), &gls.u_styles, GL_STREAM_DRAW);

    // setup default matrices for world
    memcpy(gls.u_block.m_sky, glr.skymatrix, sizeof(gls.u_block.m_sky));
    memcpy(gls.u_block.m_model, gl_identity, sizeof(gls.u_block.m_model));

    VectorCopy(glr.fd.vieworg, gls.u_block.vieworg);

    GL_Frustum(glr.fd.fov_x, glr.fd.fov_y, 1.0f);

    GL_RotateForViewer();

    // enable depth writes before clearing
    GL_StateBits(GLS_DEFAULT);

    qglClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void GL_DrawOutlines(GLsizei count, GLenum type, const void *indices)
{
    if (glr.shadowbuffer_bound)
        return;

    GL_BindTexture(TMU_TEXTURE, TEXNUM_WHITE);
    GL_StateBits(GLS_DEPTHMASK_FALSE | GLS_TEXTURE_REPLACE | (gls.state_bits & GLS_MESH_MASK));
    if (gls.currentva)
        GL_ArrayBits(GLA_VERTEX);
    GL_DepthRange(0, 0);

    if (qglPolygonMode) {
        qglPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        if (type)
            qglDrawElements(GL_TRIANGLES, count, type, indices);
        else
            qglDrawArrays(GL_TRIANGLES, 0, count);

        qglPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    } else if (type) {
        uintptr_t base = (uintptr_t)indices;
        uintptr_t size = 0;

        switch (type) {
        case GL_UNSIGNED_INT:
            size = 4 * 3;
            break;
        case GL_UNSIGNED_SHORT:
            size = 2 * 3;
            break;
        default:
            Q_assert(!"bad type");
        }

        for (int i = 0; i < count / 3; i++, base += size)
            qglDrawElements(GL_LINE_LOOP, 3, type, VBO_OFS(base));
    } else {
        for (int i = 0; i < count / 3; i++)
            qglDrawArrays(GL_LINE_LOOP, i * 3, 3);
    }

    GL_DepthRange(0, 1);
}

void GL_ForceUniforms(void)
{
    if (gls.u_block_dirty & DIRTY_MATRIX)
        GL_MultMatrix(gls.u_block.m_vp, gls.proj_matrix, gls.view_matrix);

    GL_BindBuffer(GL_UNIFORM_BUFFER, gl_static.uniform_buffers[UBO_UNIFORMS]);
    qglBufferData(GL_UNIFORM_BUFFER, sizeof(gls.u_block), &gls.u_block, GL_STREAM_DRAW);

    gls.u_block_dirty = DIRTY_NONE;
    c.uniformUploads++;
}

void GL_PushLights(uint64_t bits)
{
    if (gls.light_bits == bits)
        return;

    gls.light_bits = bits;

    if (!bits)
        return;

    int c1 = 0;
    int c2 = 0;

    for (int i = 0; i < r_numdlights; i++) {
        if (!(bits & BIT_ULL(i)))
            continue;
        if (r_dlights[i].sphere)
            gls.u_lights.dlights[c1++] = r_dlights[i].light_;
    }

    for (int i = 0; i < r_numdlights; i++) {
        if (!(bits & BIT_ULL(i)))
            continue;
        if (!r_dlights[i].sphere)
            gls.u_lights.dlights[c1 + c2++] = r_dlights[i].light_;
    }

    gls.u_lights.num_dlights[0] = c1;
    gls.u_lights.num_dlights[1] = c2;

    GL_BindBuffer(GL_UNIFORM_BUFFER, gl_static.uniform_buffers[UBO_LIGHTS]);
    qglBufferData(GL_UNIFORM_BUFFER, sizeof(gls.u_lights), NULL, GL_STREAM_DRAW);
    qglBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(gls.u_lights.num_dlights) + sizeof(glDynLight_t) * (c1 + c2), &gls.u_lights);
    c.uniformUploads++;
}

void GL_InitState(void)
{
    qglClearColor(Vector4Unpack(gl_static.clearcolor));
    GL_ClearDepth(1);
    qglClearStencil(0);

    qglEnable(GL_DEPTH_TEST);
    qglDepthFunc(GL_LEQUAL);
    GL_DepthRange(0, 1);
    qglDepthMask(GL_TRUE);
    qglDisable(GL_BLEND);
    qglFrontFace(GL_CW);
    qglCullFace(GL_BACK);
    qglEnable(GL_CULL_FACE);

    GL_ShaderStateBits(GLS_DEFAULT);

    if (gl_config.ver_gl >= QGL_VER(3, 2))
        qglEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}

void GL_ShutdownState(void)
{
    for (int i = 0; i < VERT_ATTR_COUNT; i++)
        qglDisableVertexAttribArray(i);

    memset(&gls, 0, sizeof(gls));
}
