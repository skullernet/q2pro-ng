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

typedef enum {
    SHADOW_NO,
    SHADOW_YES,
    SHADOW_ONLY
} drawshadow_t;

static unsigned         oldframenum;
static unsigned         newframenum;
static float            frontlerp;
static float            backlerp;
static float            radius;
static vec3_t           origin;
static vec4_t           meshcolor;
static uint64_t         dlightbits;
static glStateBits_t    meshbits;
static GLuint           buffer;
static float            celscale;
static float            shadowalpha;
static drawshadow_t     drawshadow;
static mat4_t           m_shadow_view;
static mat4_t           m_shadow_model;     // fog hack

static void setup_dotshading(void)
{
    float yaw, cp, cy, sp, sy;

    if (!gl_dotshading->integer)
        return;
    if (glr.ent->flags & (RF_SHELL_MASK | RF_TRACKER))
        return;
    if (drawshadow == SHADOW_ONLY)
        return;
    if (glr.shadowbuffer_bound)
        return;

    meshbits |= GLS_MESH_SHADE;

    // matches the anormtab.h precalculations
    yaw = -DEG2RAD(glr.ent->angles.yaw);
    cy = cosf(yaw);
    sy = sinf(yaw);
    cp = cosf(-M_PIf / 4);
    sp = sinf(-M_PIf / 4);
    gls.u_block.mesh.shadedir = Vec4(cp * cy, cp * sy, -sp, 0);
}

static glCullResult_t cull_static_model(const model_t *model)
{
    const maliasframe_t *newframe = &model->frames[newframenum];
    glCullResult_t cull;

    if (glr.ent->flags & RF_WEAPONMODEL) {
        cull = CULL_IN;
    } else if (glr.entrotated) {
        cull = GL_CullSphere(origin, newframe->radius);
        if (cull == CULL_OUT) {
            c.spheresCulled++;
            return cull;
        }
        if (cull == CULL_CLIP) {
            cull = GL_CullLocalBox(origin, newframe->box);
            if (cull == CULL_OUT) {
                c.rotatedBoxesCulled++;
                return cull;
            }
        }
    } else {
        cull = GL_CullBox(Box3_Translate(newframe->box, origin));
        if (cull == CULL_OUT) {
            c.boxesCulled++;
            return cull;
        }
    }

    return cull;
}

static glCullResult_t cull_lerped_model(const model_t *model)
{
    const maliasframe_t *newframe = &model->frames[newframenum];
    const maliasframe_t *oldframe = &model->frames[oldframenum];
    glCullResult_t cull;
    box3_t box;

    if (glr.ent->flags & RF_WEAPONMODEL) {
        cull = CULL_IN;
    } else if (glr.entrotated) {
        cull = GL_CullSphere(origin, max(newframe->radius, oldframe->radius));
        if (cull == CULL_OUT) {
            c.spheresCulled++;
            return cull;
        }
        if (cull == CULL_CLIP) {
            box = Box3_Union(newframe->box, oldframe->box);
            cull = GL_CullLocalBox(origin, box);
            if (cull == CULL_OUT) {
                c.rotatedBoxesCulled++;
                return cull;
            }
        }
    } else {
        box = Box3_Union(newframe->box, oldframe->box);
        box = Box3_Translate(box, origin);
        cull = GL_CullBox(box);
        if (cull == CULL_OUT) {
            c.boxesCulled++;
            return cull;
        }
    }

    return cull;
}

static void setup_frame_scale(const model_t *model)
{
    const maliasframe_t *newframe = &model->frames[newframenum];
    const maliasframe_t *oldframe = &model->frames[oldframenum];

    if (oldframenum == newframenum) {
        gls.u_block.mesh.newscale = Vec4_FromVec3(newframe->scale, 0);
        gls.u_block.mesh.translate = Vec4_FromVec3(newframe->translate, 0);
    } else {
        gls.u_block.mesh.oldscale = Vec4_FromVec3(Vec3_Scale(oldframe->scale, backlerp), 0);
        gls.u_block.mesh.newscale = Vec4_FromVec3(Vec3_Scale(newframe->scale, frontlerp), 0);
        gls.u_block.mesh.translate = Vec4_FromVec3(Vec3_Mix(oldframe->translate, newframe->translate, backlerp, frontlerp), 0);
    }
}

static void setup_lights(void)
{
    dlightbits = 0;

    if (glr.ent->flags & (RF_SHELL_MASK | RF_FULLBRIGHT | RF_TRACKER))
        return;
    if (drawshadow == SHADOW_ONLY)
        return;
    if (glr.shadowbuffer_bound)
        return;

    for (int i = 0; i < r_numdlights; i++) {
        const gldlight_t *light = &r_dlights[i];
        vec3_t dir;

        dir = Vec3_Sub(origin, light->origin);
        if (Vec3_Length(dir) > light->radius + radius) {
            c.lightsCulled++;
            continue;
        }

        // https://geometrictools.com/Documentation/IntersectionSphereCone.pdf
        if (light->cone > 0.0f) {
            float c2 = light->cone * light->cone;
            float sr = radius / sqrtf(1.0f - c2);
            dir = Vec3_MA(dir, sr, light->dir);
            float d1 = Vec3_Dot(light->dir, dir);
            float d2 = Vec3_Dot(dir, dir);
            if (d1 < 0 || d1 * d1 < d2 * c2) {
                c.lightsCulled++;
                continue;
            }
        }

        dlightbits |= BIT_ULL(i);
    }

    GL_PushLights(dlightbits);
}

static void setup_color(void)
{
    uint64_t flags = glr.ent->flags;
    vec3_t color;

    memset(&glr.lightpoint, 0, sizeof(glr.lightpoint));

    if (flags & RF_SHELL_MASK) {
        color = vec3_origin;
        if (flags & RF_SHELL_LITE_GREEN)
            color = Vec3(0.56f, 0.93f, 0.56f);
        if (flags & RF_SHELL_HALF_DAM)
            color = Vec3(0.56f, 0.59f, 0.45f);
        if (flags & RF_SHELL_DOUBLE)
            color = Vec3(0.9f, 0.7f, 0.0f);
        if (flags & RF_SHELL_RED)
            color.r = 1;
        if (flags & RF_SHELL_GREEN)
            color.g = 1;
        if (flags & RF_SHELL_BLUE)
            color.b = 1;
    } else if (flags & RF_FULLBRIGHT) {
        color = Vec3(1, 1, 1);
    } else if ((flags & RF_IR_VISIBLE) && (glr.fd.rdflags & RDF_IRGOGGLES)) {
        color = Vec3(1, 0, 0);
    } else if (flags & RF_TRACKER) {
        color = vec3_origin;
    } else {
        float f, m;

        R_LightPoint(origin, &color);

        if (flags & RF_MINLIGHT) {
            f = Vec3_Length(color);
            if (!f)
                color = Vec3(0.1f, 0.1f, 0.1f);
            else if (f < 0.1f)
                color = Vec3_Scale(color, 0.1f / f);
        }

        if (flags & RF_GLOW) {
            f = 0.1f * sinf(glr.fd.time * 7);
            for (int i = 0; i < 3; i++) {
                m = color.rgb[i] * 0.8f;
                color.rgb[i] += f;
                if (color.rgb[i] < m)
                    color.rgb[i] = m;
            }
        }
    }

    float alpha = (flags & RF_TRANSLUCENT) ? glr.ent->alpha : 1.0f;
    meshcolor = Vec4_FromVec3(color, alpha);
}

static void setup_celshading(void)
{
    celscale = 0;

    if (glr.ent->flags & (RF_TRANSLUCENT | RF_SHELL_MASK | RF_TRACKER))
        return;
    if (!qglPolygonMode || !qglLineWidth)
        return;
    if (glr.shadowbuffer_bound)
        return;
    if (gl_celshading->value <= 0)
        return;
    celscale = 1.0f - Vec3_Distance(origin, glr.fd.vieworg) / 700.0f;
}

static void draw_celshading(const uint16_t *indices, int num_indices)
{
    if (celscale < 0.01f)
        return;

    GL_BindTexture(TMU_TEXTURE, TEXNUM_BLACK);
    GL_StateBits(GLS_COLOR_ENABLE | GLS_BLEND_BLEND | (meshbits & ~GLS_MESH_SHADE) | glr.fog_bits);

    gls.u_block.mesh.color = Vec4(0, 0, 0, meshcolor.a * celscale);
    GL_ForceUniforms();

    qglLineWidth(gl_celshading->value * celscale);
    qglPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    qglCullFace(GL_FRONT);
    qglDrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, indices);
    qglCullFace(GL_BACK);
    qglPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    qglLineWidth(1);
}

static drawshadow_t cull_shadow(void)
{
    const cplane_t *plane;
    float w, dist;

    if (!gl_shadows->integer)
        return SHADOW_NO;
    if (glr.ent->flags & (RF_WEAPONMODEL | RF_NOSHADOW))
        return SHADOW_NO;
    if (!gl_config.stencilbits)
        return SHADOW_NO;
    if (glr.shadowbuffer_bound)
        return SHADOW_NO;

    setup_color();

    if (!glr.lightpoint.surf)
        return SHADOW_NO;

    // check steepness
    plane = &glr.lightpoint.plane;
    w = plane->normal.z;
    if (glr.lightpoint.surf->drawflags & DSURF_PLANEBACK)
       w = -w;
    if (w < 0.5f)
        return SHADOW_NO;   // too steep

    shadowalpha = 0.5f;

    // check if faded out
    dist = origin.z - glr.lightpoint.pos.z - radius;
    if (dist > radius * 4.0f)
        return SHADOW_NO;
    if (dist > 0)
        shadowalpha = 0.5f - dist / (radius * 8.0f);

    if (gl_cull_models->integer) {
        float min_d = -radius / w;
        for (int i = 0; i < q_countof(glr.frustum); i++) {
            if (PlaneDiff(glr.lightpoint.pos, &glr.frustum[i]) < min_d) {
                c.shadowsCulled++;
                return SHADOW_NO;   // culled out
            }
        }
    }

    return SHADOW_YES;
}

static void shadow_matrix(mat4_t matrix, const cplane_t *plane, vec3_t dir)
{
    matrix[ 0] =  plane->normal.y * dir.y + plane->normal.z * dir.z;
    matrix[ 4] = -plane->normal.y * dir.x;
    matrix[ 8] = -plane->normal.z * dir.x;
    matrix[12] =  plane->dist * dir.x;

    matrix[ 1] = -plane->normal.x * dir.y;
    matrix[ 5] =  plane->normal.x * dir.x + plane->normal.z * dir.z;
    matrix[ 9] = -plane->normal.z * dir.y;
    matrix[13] =  plane->dist * dir.y;

    matrix[ 2] = -plane->normal.x * dir.z;
    matrix[ 6] = -plane->normal.y * dir.z;
    matrix[10] =  plane->normal.x * dir.x + plane->normal.y * dir.y;
    matrix[14] =  plane->dist * dir.z;

    matrix[ 3] = 0;
    matrix[ 7] = 0;
    matrix[11] = 0;
    matrix[15] = Vec3_Dot(plane->normal, dir);
}

static void setup_shadow(void)
{
    mat4_t m_proj, m_rot;
    vec3_t dir;

    if (!drawshadow)
        return;

    // position fake light source straight over the model
    if (glr.lightpoint.surf->drawflags & DSURF_PLANEBACK)
        dir = Vec3(0, 0, -1);
    else
        dir = Vec3(0, 0, 1);

    // project shadow on ground plane
    shadow_matrix(m_proj, &glr.lightpoint.plane, dir);

    // rotate for entity
    GL_RotationMatrix(m_rot);

    GL_MultMatrix(m_shadow_model, m_proj, m_rot);
    GL_MultMatrix(m_shadow_view, glr.viewmatrix, m_shadow_model);
}

static void draw_shadow(const uint16_t *indices, int num_indices)
{
    if (!drawshadow)
        return;

    // fog hack
    if (glr.fog_bits)
        memcpy(gls.u_block.m_model, m_shadow_model, sizeof(gls.u_block.m_model));

    // load shadow projection matrix
    GL_LoadMatrix(m_shadow_view);

    // eliminate z-fighting by utilizing stencil buffer, if available
    qglEnable(GL_STENCIL_TEST);
    qglStencilFunc(GL_EQUAL, 0, 0xff);
    qglStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    GL_BindTexture(TMU_TEXTURE, TEXNUM_WHITE);
    GL_StateBits(GLS_COLOR_ENABLE | GLS_BLEND_BLEND | (meshbits & ~GLS_MESH_SHADE) | glr.fog_bits);

    gls.u_block.mesh.color = Vec4(0, 0, 0, meshcolor.a * shadowalpha);
    GL_ForceUniforms();

    qglEnable(GL_POLYGON_OFFSET_FILL);
    qglPolygonOffset(-1.0f, -2.0f);
    qglDrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, indices);
    qglDisable(GL_POLYGON_OFFSET_FILL);

    qglDisable(GL_STENCIL_TEST);

    // fog hack
    if (glr.fog_bits)
        GL_RotationMatrix(gls.u_block.m_model);
}

static const image_t *skin_for_mesh(image_t **skins, int num_skins)
{
    const glentity_t *ent = glr.ent;

    if (ent->flags & RF_SHELL_MASK)
        return R_SHELLTEXTURE;

    if (ent->skin)
        return IMG_ForHandle(ent->skin);

    if (!num_skins)
        return R_NOTEXTURE;

    if (ent->skinnum < 0 || ent->skinnum >= num_skins)
        return skins[0];

    if (skins[ent->skinnum] == R_NOTEXTURE)
        return skins[0];

    return skins[ent->skinnum];
}

static void bind_alias_arrays(const maliasmesh_t *mesh)
{
    uintptr_t base = (uintptr_t)mesh->verts;
    uintptr_t old_ofs = base + oldframenum * mesh->numverts * sizeof(mesh->verts[0]);
    uintptr_t new_ofs = base + newframenum * mesh->numverts * sizeof(mesh->verts[0]);

    qglVertexAttribPointer(VERT_ATTR_MESH_TC, 2, GL_FLOAT, GL_FALSE, 0, mesh->tcoords);
    qglVertexAttribIPointer(VERT_ATTR_MESH_NEW_POS, 4, GL_SHORT, 0, VBO_OFS(new_ofs));

    if (oldframenum == newframenum) {
        GL_ArrayBits(GLA_MESH_STATIC);
    } else {
        qglVertexAttribIPointer(VERT_ATTR_MESH_OLD_POS, 4, GL_SHORT, 0, VBO_OFS(old_ofs));
        GL_ArrayBits(GLA_MESH_LERP);
    }
}

static void draw_alias_mesh(const uint16_t *indices, int num_indices,
                            const maliastc_t *tcoords, int num_verts,
                            image_t **skins, int num_skins)
{
    glStateBits_t state;
    const image_t *skin;
    const uint64_t flags = glr.ent->flags;

    c.trisDrawn += num_indices / 3;

    // if the model was culled, just draw the shadow
    if (drawshadow == SHADOW_ONLY) {
        GL_LockArrays(num_verts);
        draw_shadow(indices, num_indices);
        GL_UnlockArrays();
        return;
    }

    // fall back to entity matrix
    GL_LoadMatrix(glr.entmatrix);

    gls.u_block.mesh.color = meshcolor;
    GL_ForceUniforms();

    // avoid drawing hidden faces by pre-filling depth buffer, but not for
    // explosions and muzzleflashes
    if ((flags & (RF_FULLBRIGHT | RF_TRANSLUCENT)) == RF_TRANSLUCENT) {
        GL_StateBits(meshbits & ~GLS_MESH_SHADE);
        GL_BindTexture(TMU_TEXTURE, TEXNUM_WHITE);
        qglColorMask(0, 0, 0, 0);

        GL_LockArrays(num_verts);
        qglDrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, indices);
        GL_UnlockArrays();

        qglColorMask(1, 1, 1, 1);
    }

    if (glr.shadowbuffer_bound) {
        state = meshbits;
        GL_BindTexture(TMU_TEXTURE, TEXNUM_BLACK);
    } else {
        state = GLS_COLOR_ENABLE | glr.fog_bits | meshbits;

        if (dlightbits) {
            state |= GLS_DYNAMIC_LIGHTS;
            if (glr.shadowbuffer_ok && gl_shadowmap->integer)
                state |= GLS_SHADOWMAP_ENABLE;
        }

        if (flags & RF_TRANSLUCENT)
            state |= GLS_BLEND_BLEND | GLS_DEPTHMASK_FALSE;

        skin = skin_for_mesh(skins, num_skins);
        if (skin->texnum2)
            state |= GLS_GLOWMAP_ENABLE;

        if (glr.framebuffer_bound && gl_bloom->integer) {
            state |= GLS_BLOOM_GENERATE;
            if (flags & RF_SHELL_MASK)
                state |= GLS_BLOOM_SHELL;
        }

        GL_BindTexture(TMU_TEXTURE, skin->texnum);

        if (skin->texnum2)
            GL_BindTexture(TMU_GLOWMAP, skin->texnum2);
    }

    GL_StateBits(state);

    GL_LockArrays(num_verts);

    qglDrawElements(GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, indices);

    draw_celshading(indices, num_indices);

    if (gl_showtris->integer & SHOWTRIS_MESH)
        GL_DrawOutlines(num_indices, GL_UNSIGNED_SHORT, indices);

    // FIXME: unlock arrays before changing matrix?
    draw_shadow(indices, num_indices);

    GL_UnlockArrays();
}

#if USE_MD5

static const md5_joint_t *lerp_alias_skeleton(const md5_model_t *model)
{
    unsigned frame_a = oldframenum % model->num_frames;
    unsigned frame_b = newframenum % model->num_frames;
    const md5_joint_t *skel_a = &model->skeleton_frames[frame_a * model->num_joints];
    const md5_joint_t *skel_b = &model->skeleton_frames[frame_b * model->num_joints];
    static md5_joint_t temp_skeleton[MD5_MAX_JOINTS];
    md5_joint_t *out = temp_skeleton;

    for (int i = 0; i < model->num_joints; i++, skel_a++, skel_b++, out++) {
        out->scale  = skel_b->scale;
        out->pos    = Vec3_Mix(skel_a->pos, skel_b->pos, backlerp, frontlerp);
        out->orient = Quat_SLerp(skel_a->orient, skel_b->orient, backlerp, frontlerp);
        Quat_ToAxis(out->orient, out->axis);
    }

    return temp_skeleton;
}

static void bind_skel_arrays(const md5_mesh_t *mesh)
{
    if (gl_config.caps & QGL_CAP_SHADER_STORAGE) {
        qglBindBufferRange(GL_SHADER_STORAGE_BUFFER, SSBO_WEIGHTS, buffer,
                           (uintptr_t)mesh->weights, mesh->num_weights * sizeof(mesh->weights[0]));
        qglBindBufferRange(GL_SHADER_STORAGE_BUFFER, SSBO_JOINTNUMS, buffer,
                           (uintptr_t)mesh->jointnums, Q_ALIGN(mesh->num_weights, sizeof(uint32_t)));
    } else {
        Q_assert(gl_config.caps & QGL_CAP_BUFFER_TEXTURE);

        gls.u_block.mesh.weight_ofs   = (uintptr_t)mesh->weights / sizeof(mesh->weights[0]);
        gls.u_block.mesh.jointnum_ofs = (uintptr_t)mesh->jointnums;

        GL_ActiveTexture(TMU_SKEL_WEIGHTS);
        qglBindTexture(GL_TEXTURE_BUFFER, gl_static.skeleton_tex[SSBO_WEIGHTS]);
        qglTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, buffer);

        GL_ActiveTexture(TMU_SKEL_JOINTNUMS);
        qglBindTexture(GL_TEXTURE_BUFFER, gl_static.skeleton_tex[SSBO_JOINTNUMS]);
        qglTexBuffer(GL_TEXTURE_BUFFER, GL_R8UI, buffer);
    }

    uintptr_t base = (uintptr_t)mesh->vertices;
    qglVertexAttribPointer (VERT_ATTR_MESH_TC,   2, GL_FLOAT, GL_FALSE, 0, mesh->tcoords);
    qglVertexAttribPointer (VERT_ATTR_MESH_NORM, 3, GL_FLOAT, GL_FALSE, sizeof(mesh->vertices[0]), VBO_OFS(base));
    qglVertexAttribIPointer(VERT_ATTR_MESH_VERT, 2, GL_UNSIGNED_SHORT,  sizeof(mesh->vertices[0]), VBO_OFS(base + sizeof(vec3_t)));

    GL_ArrayBits(GLA_MESH_LERP);
}

static void draw_alias_skeleton(const md5_model_t *model)
{
    const md5_joint_t *skel;
    glJoint_t joints[MD5_MAX_JOINTS];

    if (newframenum == oldframenum)
        skel = &model->skeleton_frames[newframenum % model->num_frames * model->num_joints];
    else
        skel = lerp_alias_skeleton(model);

    for (int i = 0; i < model->num_joints; i++) {
        const md5_joint_t *in = &skel[i];
        glJoint_t *out = &joints[i];
        out->pos = Vec4_FromVec3(in->pos, in->scale);
        out->axis[0] = Vec4_FromVec3(in->axis[0], 0);
        out->axis[1] = Vec4_FromVec3(in->axis[1], 0);
        out->axis[2] = Vec4_FromVec3(in->axis[2], 0);
    }

    GL_BindBuffer(GL_UNIFORM_BUFFER, gl_static.uniform_buffers[UBO_SKELETON]);
    qglBufferData(GL_UNIFORM_BUFFER, sizeof(joints), NULL, GL_STREAM_DRAW);
    qglBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(joints[0]) * model->num_joints, joints);

    meshbits &= ~GLS_MESH_MD2;
    meshbits |=  GLS_MESH_MD5 | GLS_MESH_LERP;

    for (int i = 0; i < model->num_meshes; i++) {
        const md5_mesh_t *mesh = &model->meshes[i];
        bind_skel_arrays(mesh);
        draw_alias_mesh(mesh->indices, mesh->num_indices,
                        mesh->tcoords, mesh->num_verts,
                        model->skins, model->num_skins);
    }
}

#endif  // USE_MD5

// extra ugly. this needs to be done on the client, but to avoid complexity of
// rendering gun model in its own refdef, and to preserve compatibility with
// existing RF_WEAPONMODEL flag, we do it here.
static void setup_weaponmodel(const glentity_t *ent)
{
    float fov_x = glr.fd.fov_x;
    float fov_y = glr.fd.fov_y;
    float reflect_x = 1.0f;

    if (ent->flags & RF_FOVHACK) {
        fov_x = ent->oldorigin.x;
        fov_y = ent->oldorigin.y;
    }

    if (ent->flags & RF_LEFTHAND) {
        reflect_x = -1.0f;
        qglFrontFace(GL_CCW);
    }

    GL_Frustum(fov_x, fov_y, reflect_x);
}

void GL_DrawAliasModel(const model_t *model)
{
    const glentity_t *ent = glr.ent;
    glCullResult_t cull;

    newframenum = ent->frame % model->numframes;
    oldframenum = ent->oldframe % model->numframes;

    backlerp = ent->backlerp;
    frontlerp = 1.0f - backlerp;

    radius = (model->frames[newframenum].radius * frontlerp + model->frames[oldframenum].radius * backlerp) * glr.entscale;

    // optimized case
    if (backlerp == 0)
        oldframenum = newframenum;

    origin = ent->origin;

    // cull the shadow
    drawshadow = cull_shadow();

    // cull the model
    if (newframenum == oldframenum)
        cull = cull_static_model(model);
    else
        cull = cull_lerped_model(model);
    if (cull == CULL_OUT) {
        if (!drawshadow)
            return;
        drawshadow = SHADOW_ONLY;   // still need to draw the shadow
    }

    meshbits = GLS_MESH_MD2;
    if (oldframenum != newframenum)
        meshbits |= GLS_MESH_LERP;

    // setup parameters common for all meshes
    if (!drawshadow)
        setup_color();
    setup_celshading();
    setup_dotshading();
    setup_shadow();
    setup_lights();

    // setup scale and translate vectors
    setup_frame_scale(model);

    if (ent->flags & RF_SHELL_MASK) {
        gls.u_block.mesh.shellscale = (ent->flags & RF_WEAPONMODEL) ? WEAPONSHELL_SCALE : POWERSUIT_SCALE;
        meshbits |= GLS_MESH_SHELL;
    }

    GL_BindArrays(VA_NONE);

    buffer = model->buffers[0];
    GL_BindBuffer(GL_ARRAY_BUFFER, model->buffers[0]);
    GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, model->buffers[1]);

    gls.u_block.mesh.backlerp = backlerp;
    gls.u_block.mesh.frontlerp = frontlerp;

    GL_RotateForEntity();

    if (ent->flags & RF_WEAPONMODEL)
        setup_weaponmodel(ent);

    if (ent->flags & RF_DEPTHHACK)
        GL_DepthRange(0, 0.25f);

    // draw all the meshes
#if USE_MD5
    if (model->skeleton && gl_md5_use->integer &&
        (ent->flags & RF_NO_LOD || gl_md5_distance->value <= 0 ||
         Vec3_Distance(origin, glr.fd.vieworg) <= gl_md5_distance->value * glr.entscale))
        draw_alias_skeleton(model->skeleton);
    else
#endif
    for (int i = 0; i < model->nummeshes; i++) {
        const maliasmesh_t *mesh = &model->meshes[i];
        bind_alias_arrays(mesh);
        draw_alias_mesh(mesh->indices, mesh->numindices,
                        mesh->tcoords, mesh->numverts,
                        mesh->skins, mesh->numskins);
    }

    if (ent->flags & RF_DEPTHHACK)
        GL_DepthRange(0, 1);

    if (ent->flags & RF_WEAPONMODEL) {
        GL_Frustum(glr.fd.fov_x, glr.fd.fov_y, 1.0f);
        qglFrontFace(GL_CW);
    }
}
