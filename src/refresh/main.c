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

/*
 * gl_main.c
 *
 */

#include "gl.h"

glRefdef_t glr;
glStatic_t gl_static;
glConfig_t gl_config;
statCounters_t  c;

glentity_t gl_world;

refcfg_t r_config;

int         r_numdlights;
dlight_t    r_dlights[MAX_DLIGHTS];

int         r_numentities;
glentity_t  r_entities[MAX_ENTITIES];

int                 r_numparticles;
const particle_t    *r_particles;

unsigned    r_registration_sequence;

// regular variables
cvar_t *gl_partscale;
cvar_t *gl_partstyle;
cvar_t *gl_beamstyle;
cvar_t *gl_celshading;
cvar_t *gl_dotshading;
cvar_t *gl_shadowmap;
cvar_t *gl_shadows;
cvar_t *gl_modulate;
cvar_t *gl_modulate_world;
cvar_t *gl_modulate_entities;
cvar_t *gl_dynamic;
cvar_t *gl_flarespeed;
cvar_t *gl_fontshadow;
#if USE_MD5
cvar_t *gl_md5_load;
cvar_t *gl_md5_use;
cvar_t *gl_md5_distance;
#endif
cvar_t *gl_damageblend_frac;
cvar_t *gl_waterwarp;
cvar_t *gl_fog;
cvar_t *gl_bloom;
cvar_t *gl_bloom_sigma;
cvar_t *gl_swapinterval;

// development variables
cvar_t *gl_znear;
cvar_t *gl_drawworld;
cvar_t *gl_drawentities;
cvar_t *gl_drawsky;
cvar_t *gl_draworder;
cvar_t *gl_showtris;
cvar_t *gl_showorigins;
cvar_t *gl_showtearing;
cvar_t *gl_showbloom;
#if USE_DEBUG
cvar_t *gl_showstats;
cvar_t *gl_showscrap;
cvar_t *gl_nobind;
cvar_t *gl_test;
#endif
cvar_t *gl_cull_nodes;
cvar_t *gl_cull_models;
cvar_t *gl_clear;
cvar_t *gl_clearcolor;
cvar_t *gl_finish;
cvar_t *gl_novis;
cvar_t *gl_lockpvs;
cvar_t *gl_lightmap;
cvar_t *gl_lightmap_bits;
cvar_t *gl_fullbright;
cvar_t *gl_lightgrid;
cvar_t *gl_polyblend;
cvar_t *gl_showerrors;

// ==============================================================================

void GL_SetupFrustum(float zfar)
{
    vec_t angle, sf, cf;
    vec3_t forward, left, up;
    cplane_t *p;
    int i;

    // right/left
    angle = DEG2RAD(glr.fd.fov_x / 2);
    sf = sinf(angle);
    cf = cosf(angle);

    VectorScale(glr.viewaxis[0], sf, forward);
    VectorScale(glr.viewaxis[1], cf, left);

    VectorAdd(forward, left, glr.frustum[0].normal);
    VectorSubtract(forward, left, glr.frustum[1].normal);

    // top/bottom
    angle = DEG2RAD(glr.fd.fov_y / 2);
    sf = sinf(angle);
    cf = cosf(angle);

    VectorScale(glr.viewaxis[0], sf, forward);
    VectorScale(glr.viewaxis[2], cf, up);

    VectorAdd(forward, up, glr.frustum[2].normal);
    VectorSubtract(forward, up, glr.frustum[3].normal);

    // far
    VectorNegate(glr.viewaxis[0], glr.frustum[4].normal);

    for (i = 0, p = glr.frustum; i < q_countof(glr.frustum); i++, p++) {
        p->dist = DotProduct(glr.fd.vieworg, p->normal);
        p->type = PLANE_NON_AXIAL;
        SetPlaneSignbits(p);
    }

    glr.frustum[4].dist -= zfar;
}

glCullResult_t GL_CullBox(const vec3_t bounds[2])
{
    box_plane_t bits;
    glCullResult_t cull;

    if (!gl_cull_models->integer)
        return CULL_IN;

    cull = CULL_IN;
    for (int i = 0; i < q_countof(glr.frustum); i++) {
        bits = BoxOnPlaneSide(bounds[0], bounds[1], &glr.frustum[i]);
        if (bits == BOX_BEHIND)
            return CULL_OUT;
        if (bits != BOX_INFRONT)
            cull = CULL_CLIP;
    }

    return cull;
}

glCullResult_t GL_CullSphere(const vec3_t origin, float radius)
{
    float dist;
    const cplane_t *p;
    glCullResult_t cull;
    int i;

    if (!gl_cull_models->integer)
        return CULL_IN;

    radius *= glr.entscale;
    cull = CULL_IN;
    for (i = 0, p = glr.frustum; i < q_countof(glr.frustum); i++, p++) {
        dist = PlaneDiff(origin, p);
        if (dist < -radius)
            return CULL_OUT;
        if (dist <= radius)
            cull = CULL_CLIP;
    }

    return cull;
}

glCullResult_t GL_CullLocalBox(const vec3_t origin, const vec3_t bounds[2])
{
    vec3_t points[8];
    const cplane_t *p;
    int i, j;
    vec_t dot;
    bool infront;
    glCullResult_t cull;

    if (!gl_cull_models->integer)
        return CULL_IN;

    for (i = 0; i < 8; i++) {
        VectorCopy(origin, points[i]);
        VectorMA(points[i], bounds[(i >> 0) & 1][0], glr.entaxis[0], points[i]);
        VectorMA(points[i], bounds[(i >> 1) & 1][1], glr.entaxis[1], points[i]);
        VectorMA(points[i], bounds[(i >> 2) & 1][2], glr.entaxis[2], points[i]);
    }

    cull = CULL_IN;
    for (i = 0, p = glr.frustum; i < q_countof(glr.frustum); i++, p++) {
        infront = false;
        for (j = 0; j < 8; j++) {
            dot = DotProduct(points[j], p->normal);
            if (dot >= p->dist) {
                infront = true;
                if (cull == CULL_CLIP)
                    break;
            } else {
                cull = CULL_CLIP;
                if (infront)
                    break;
            }
        }
        if (!infront)
            return CULL_OUT;
    }

    return cull;
}

// shared between lightmap and scrap allocators
bool GL_AllocBlock(int width, int height, uint16_t *inuse,
                   int w, int h, int *s, int *t)
{
    int i, j, k, x, y, max_inuse, min_inuse;

    x = 0; y = height;
    min_inuse = height;
    for (i = 0; i < width - w; i++) {
        max_inuse = 0;
        for (j = 0; j < w; j++) {
            k = inuse[i + j];
            if (k >= min_inuse)
                break;
            if (max_inuse < k)
                max_inuse = k;
        }
        if (j == w) {
            x = i;
            y = min_inuse = max_inuse;
        }
    }

    if (y + h > height)
        return false;

    for (i = 0; i < w; i++)
        inuse[x + i] = y + h;

    *s = x;
    *t = y;
    return true;
}

// P = A * B
void GL_MultMatrix(GLfloat *restrict p, const GLfloat *restrict a, const GLfloat *restrict b)
{
    Matrix_Multiply(a, b, p);
}

void GL_SetEntityAxis(void)
{
    const glentity_t *e = glr.ent;

    glr.entrotated = false;
    glr.entscale = 1;

    if (VectorEmpty(e->angles)) {
        AxisClear(glr.entaxis);
    } else {
        AnglesToAxis(e->angles, glr.entaxis);
        glr.entrotated = true;
    }

    if (e->scale && e->scale != 1) {
        VectorScale(glr.entaxis[0], e->scale, glr.entaxis[0]);
        VectorScale(glr.entaxis[1], e->scale, glr.entaxis[1]);
        VectorScale(glr.entaxis[2], e->scale, glr.entaxis[2]);
        glr.entrotated = true;
        glr.entscale = e->scale;
    }
}

void GL_RotationMatrix(GLfloat *matrix)
{
    matrix[ 0] = glr.entaxis[0][0];
    matrix[ 4] = glr.entaxis[1][0];
    matrix[ 8] = glr.entaxis[2][0];
    matrix[12] = glr.ent->origin[0];

    matrix[ 1] = glr.entaxis[0][1];
    matrix[ 5] = glr.entaxis[1][1];
    matrix[ 9] = glr.entaxis[2][1];
    matrix[13] = glr.ent->origin[1];

    matrix[ 2] = glr.entaxis[0][2];
    matrix[ 6] = glr.entaxis[1][2];
    matrix[10] = glr.entaxis[2][2];
    matrix[14] = glr.ent->origin[2];

    matrix[ 3] = 0;
    matrix[ 7] = 0;
    matrix[11] = 0;
    matrix[15] = 1;
}

void GL_RotateForEntity(void)
{
    GL_RotationMatrix(gls.u_block.m_model);
    if (glr.ent == &gl_world || (glr.ent->model & BIT(31) && !(gl_static.nodraw_mask & SURF_SKY))) {
        GL_MultMatrix(gls.u_block.m_sky[0], glr.skymatrix[0], gls.u_block.m_model);
        GL_MultMatrix(gls.u_block.m_sky[1], glr.skymatrix[1], gls.u_block.m_model);
    }
    GL_MultMatrix(glr.entmatrix, glr.viewmatrix, gls.u_block.m_model);
    GL_ForceMatrix(glr.entmatrix);
}

static void GL_DrawSpriteModel(const model_t *model)
{
    const glentity_t *e = glr.ent;
    const mspriteframe_t *frame = &model->spriteframes[e->frame % model->numframes];
    const image_t *image = frame->image;
    const float alpha = (e->flags & RF_TRANSLUCENT) ? e->alpha : 1.0f;
    const float scale = e->scale ? e->scale : 1.0f;
    glStateBits_t bits = GLS_DEPTHMASK_FALSE | glr.fog_bits;
    vec3_t up, down, left, right;

    if (glr.shadowbuffer_bound)
        return;

    if (alpha == 1.0f) {
        if (image->flags & IF_TRANSPARENT) {
            if (image->flags & IF_PALETTED)
                bits |= GLS_ALPHATEST_ENABLE;
            else
                bits |= GLS_BLEND_BLEND;
        }
    } else {
        bits |= GLS_BLEND_BLEND;
    }

    GL_LoadMatrix(glr.viewmatrix);
    GL_LoadUniforms();
    GL_BindTexture(TMU_TEXTURE, image->texnum);
    GL_BindArrays(VA_SPRITE);
    GL_StateBits(bits);
    GL_ArrayBits(GLA_VERTEX | GLA_TC);
    GL_Color(1, 1, 1, alpha);

    VectorScale(glr.viewaxis[1], frame->origin_x * scale, left);
    VectorScale(glr.viewaxis[1], (frame->origin_x - frame->width) * scale, right);
    VectorScale(glr.viewaxis[2], -frame->origin_y * scale, down);
    VectorScale(glr.viewaxis[2], (frame->height - frame->origin_y) * scale, up);

    VectorAdd3(e->origin, down, left,  tess.vertices);
    VectorAdd3(e->origin, up,   left,  tess.vertices +  5);
    VectorAdd3(e->origin, down, right, tess.vertices + 10);
    VectorAdd3(e->origin, up,   right, tess.vertices + 15);

    tess.vertices[ 3] = 0; tess.vertices[ 4] = 1;
    tess.vertices[ 8] = 0; tess.vertices[ 9] = 0;
    tess.vertices[13] = 1; tess.vertices[14] = 1;
    tess.vertices[18] = 1; tess.vertices[19] = 0;

    GL_LockArrays(4);
    qglDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    GL_UnlockArrays();
}

static void GL_DrawNullModel(void)
{
    const glentity_t *e = glr.ent;

    if (e->flags & RF_WEAPONMODEL)
        return;
    if (glr.shadowbuffer_bound)
        return;

    VectorCopy(e->origin, tess.vertices +  0);
    VectorCopy(e->origin, tess.vertices +  8);
    VectorCopy(e->origin, tess.vertices + 16);

    VectorMA(e->origin, 16, glr.entaxis[0], tess.vertices +  4);
    VectorMA(e->origin, 16, glr.entaxis[1], tess.vertices + 12);
    VectorMA(e->origin, 16, glr.entaxis[2], tess.vertices + 20);

    WN32(tess.vertices +  3, U32_RED);
    WN32(tess.vertices +  7, U32_RED);

    WN32(tess.vertices + 11, U32_GREEN);
    WN32(tess.vertices + 15, U32_GREEN);

    WN32(tess.vertices + 19, U32_BLUE);
    WN32(tess.vertices + 23, U32_BLUE);

    GL_LoadMatrix(glr.viewmatrix);
    GL_LoadUniforms();
    GL_BindTexture(TMU_TEXTURE, TEXNUM_WHITE);
    GL_BindArrays(VA_NULLMODEL);
    GL_StateBits(GLS_DEFAULT);
    GL_ArrayBits(GLA_VERTEX | GLA_COLOR);

    GL_LockArrays(6);
    qglDrawArrays(GL_LINES, 0, 6);
    GL_UnlockArrays();
}

static void make_flare_quad(const vec3_t origin, float scale)
{
    vec3_t up, down, left, right;

    VectorScale(glr.viewaxis[1],  scale, left);
    VectorScale(glr.viewaxis[1], -scale, right);
    VectorScale(glr.viewaxis[2], -scale, down);
    VectorScale(glr.viewaxis[2],  scale, up);

    VectorAdd3(origin, down, left,  tess.vertices + 0);
    VectorAdd3(origin, up,   left,  tess.vertices + 3);
    VectorAdd3(origin, down, right, tess.vertices + 6);
    VectorAdd3(origin, up,   right, tess.vertices + 9);
}

static void GL_OccludeFlares(void)
{
    const bsp_t *bsp = gl_static.world.cache;
    const glentity_t *ent;
    glquery_t *q;
    vec3_t dir, org;
    float scale, dist;
    bool set = false;
    int i;

    for (ent = glr.ents.flares; ent; ent = ent->next) {
        q = HashMap_Lookup(glquery_t, gl_static.queries, &ent->skinnum);

        for (i = 0; i < q_countof(glr.frustum); i++)
            if (PlaneDiff(ent->origin, &glr.frustum[i]) < -2.5f)
                break;
        if (i != q_countof(glr.frustum)) {
            if (q)
                q->pending = q->visible = false;
            continue;   // not visible
        }

        if (q) {
            // reset visibility if entity disappeared
            if (com_eventTime - q->timestamp >= 2500) {
                q->pending = q->visible = false;
                q->frac = 0;
            } else {
                if (q->pending)
                    continue;
                if (com_eventTime - q->timestamp <= 33)
                    continue;
            }
        } else {
            glquery_t new = { 0 };
            uint32_t map_size = HashMap_Size(gl_static.queries);
            Q_assert(map_size < MAX_EDICTS);
            qglGenQueries(1, &new.query);
            HashMap_Insert(gl_static.queries, &ent->skinnum, &new);
            q = HashMap_GetValue(glquery_t, gl_static.queries, map_size);
        }

        if (!set) {
            GL_LoadMatrix(glr.viewmatrix);
            GL_LoadUniforms();
            GL_BindTexture(TMU_TEXTURE, TEXNUM_WHITE);
            GL_BindArrays(VA_OCCLUDE);
            GL_StateBits(GLS_DEPTHMASK_FALSE);
            GL_ArrayBits(GLA_VERTEX);
            qglColorMask(0, 0, 0, 0);
            set = true;
        }

        VectorSubtract(ent->origin, glr.fd.vieworg, dir);
        dist = DotProduct(dir, glr.viewaxis[0]);

        scale = 2.5f;
        if (dist > 20)
            scale += dist * 0.004f;

        if (bsp && BSP_PointLeaf(bsp->nodes, ent->origin)->contents & CONTENTS_SOLID) {
            VectorNormalize(dir);
            VectorMA(ent->origin, -5.0f, dir, org);
            make_flare_quad(org, scale);
        } else
            make_flare_quad(ent->origin, scale);

        GL_LockArrays(4);
        qglBeginQuery(gl_static.samples_passed, q->query);
        qglDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        qglEndQuery(gl_static.samples_passed);
        GL_UnlockArrays();

        q->timestamp = com_eventTime;
        q->pending = true;

        c.occlusionQueries++;
    }

    if (set)
        qglColorMask(1, 1, 1, 1);
}

// renderer will iterate the list backwards, so sorting order must be reversed
static int entitycmpfnc(const void *_a, const void *_b)
{
    const glentity_t *a = (const glentity_t *)_a;
    const glentity_t *b = (const glentity_t *)_b;

    bool a_trans = a->flags & RF_TRANSLUCENT;
    bool b_trans = b->flags & RF_TRANSLUCENT;
    if (a_trans != b_trans)
        return b_trans - a_trans;
    if (a_trans) {
        float dist_a = DistanceSquared(a->origin, glr.fd.vieworg);
        float dist_b = DistanceSquared(b->origin, glr.fd.vieworg);
        if (dist_a > dist_b)
            return 1;
        if (dist_a < dist_b)
            return -1;
    }

    bool a_shell = a->flags & RF_SHELL_MASK;
    bool b_shell = b->flags & RF_SHELL_MASK;
    if (a_shell != b_shell)
        return b_shell - a_shell;

    // all other models are sorted by model then skin
    if (a->model > b->model)
        return -1;
    if (a->model < b->model)
        return 1;

    if (a->skin > b->skin)
        return -1;
    if (a->skin < b->skin)
        return 1;

    return 0;
}

static void GL_SortEntities(void)
{
    glentity_t *ent;
    int i;

    memset(&glr.ents, 0, sizeof(glr.ents));

    if (!gl_drawentities->integer)
        return;

    // sort entities for better cache locality
    qsort(r_entities, r_numentities, sizeof(r_entities[0]), entitycmpfnc);

    for (i = 0, ent = r_entities; i < r_numentities; i++, ent++) {
        if (ent->flags & RF_BEAM) {
            if (ent->frame) {
                ent->next = glr.ents.beams;
                glr.ents.beams = ent;
            }
            continue;
        }

        if (ent->flags & RF_FLARE) {
            if (gl_static.queries) {
                ent->next = glr.ents.flares;
                glr.ents.flares = ent;
            }
            continue;
        }

        if (ent->model & BIT(31)) {
            ent->next = glr.ents.bmodels;
            glr.ents.bmodels = ent;
            continue;
        }

        if (!(ent->flags & RF_TRANSLUCENT)) {
            ent->next = glr.ents.opaque;
            glr.ents.opaque = ent;
            continue;
        }

        if ((ent->flags & RF_WEAPONMODEL) || ent->alpha <= gl_draworder->value) {
            ent->next = glr.ents.alpha_front;
            glr.ents.alpha_front = ent;
            continue;
        }

        ent->next = glr.ents.alpha_back;
        glr.ents.alpha_back = ent;
    }
}

void GL_DrawEntities(glentity_t *ent, int exclude)
{
    model_t *model;

    for (; ent; ent = ent->next) {
        if (ent->flags & exclude)
            continue;

        glr.ent = ent;

        // convert angles to axis
        GL_SetEntityAxis();

        // inline BSP model
        if (ent->model & BIT(31)) {
            const bsp_t *bsp = gl_static.world.cache;
            int index = ~ent->model;

            if (!bsp)
                Com_Error(ERR_DROP, "%s: inline model without world",
                          __func__);

            if (index < 1 || index >= bsp->nummodels)
                Com_Error(ERR_DROP, "%s: inline model %d out of range",
                          __func__, index);

            GL_DrawBspModel(&bsp->models[index]);
            continue;
        }

        model = MOD_ForHandle(ent->model);
        if (!model) {
            GL_DrawNullModel();
            continue;
        }

        switch (model->type) {
        case MOD_ALIAS:
            GL_DrawAliasModel(model);
            break;
        case MOD_SPRITE:
            GL_DrawSpriteModel(model);
            break;
        case MOD_EMPTY:
            break;
        default:
            Q_assert(!"bad model type");
        }

        if (gl_showorigins->integer)
            GL_DrawNullModel();
    }
}

static void GL_DrawTearing(void)
{
    static int i;

    // alternate colors to make tearing obvious
    i++;
    if (i & 1)
        qglClearColor(1, 1, 1, 1);
    else
        qglClearColor(1, 0, 0, 0);

    qglClear(GL_COLOR_BUFFER_BIT);
    qglClearColor(Vector4Unpack(gl_static.clearcolor));
}

static const char *GL_ErrorString(GLenum err)
{
    switch (err) {
#define E(x) case GL_##x: return "GL_"#x;
        E(NO_ERROR)
        E(INVALID_ENUM)
        E(INVALID_VALUE)
        E(INVALID_OPERATION)
        E(STACK_OVERFLOW)
        E(STACK_UNDERFLOW)
        E(OUT_OF_MEMORY)
#undef E
    }

    return "UNKNOWN ERROR";
}

void GL_ClearErrors(void)
{
    GLenum err;

    while ((err = qglGetError()) != GL_NO_ERROR)
        ;
}

bool GL_ShowErrors(const char *func)
{
    GLenum err = qglGetError();

    if (err == GL_NO_ERROR)
        return false;

    do {
        if (gl_showerrors->integer)
            Com_EPrintf("%s: %s\n", func, GL_ErrorString(err));
    } while ((err = qglGetError()) != GL_NO_ERROR);

    return true;
}

static void GL_PostProcess(glStateBits_t bits, int x, int y, int w, int h)
{
    GL_BindArrays(VA_POSTPROCESS);
    GL_StateBits(GLS_DEPTHTEST_DISABLE | GLS_DEPTHMASK_FALSE |
                 GLS_CULL_DISABLE | GLS_TEXTURE_REPLACE | bits);
    GL_ArrayBits(GLA_VERTEX | GLA_TC);
    GL_ForceUniforms();

    Vector4Set(tess.vertices,      x,     y,     0, 1);
    Vector4Set(tess.vertices +  4, x,     y + h, 0, 0);
    Vector4Set(tess.vertices +  8, x + w, y,     1, 1);
    Vector4Set(tess.vertices + 12, x + w, y + h, 1, 0);

    GL_LockArrays(4);
    qglDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    GL_UnlockArrays();
}

static void GL_DrawBloom(bool waterwarp)
{
    int iterations = Cvar_ClampInteger(gl_bloom, 1, 8) * 2;
    int w = glr.fd.width / 4;
    int h = glr.fd.height / 4;

    qglViewport(0, 0, w, h);
    GL_Ortho(0, w, h, 0, -1, 1);

    // downscale
    gls.u_block.fog_color[0] = 1.0f / w;
    gls.u_block.fog_color[1] = 1.0f / h;
    GL_ForceTexture(TMU_TEXTURE, TEXNUM_PP_BLOOM);
    qglBindFramebuffer(GL_FRAMEBUFFER, FBO_BLUR_0);
    GL_PostProcess(GLS_BLUR_BOX, 0, 0, w, h);

    // blur X/Y
    for (int i = 0; i < iterations; i++) {
        int j = i & 1;

        gls.u_block.fog_color[0] = 1.0f / w;
        gls.u_block.fog_color[1] = 1.0f / h;
        gls.u_block.fog_color[j] = 0;

        GL_ForceTexture(TMU_TEXTURE, j ? TEXNUM_PP_BLUR_1 : TEXNUM_PP_BLUR_0);
        qglBindFramebuffer(GL_FRAMEBUFFER, j ? FBO_BLUR_0 : FBO_BLUR_1);
        GL_PostProcess(GLS_BLUR_GAUSS, 0, 0, w, h);
    }

    GL_Setup2D();

    glStateBits_t bits = GLS_BLOOM_OUTPUT;
    if (q_unlikely(gl_showbloom->integer)) {
        GL_ForceTexture(TMU_TEXTURE, TEXNUM_PP_BLUR_0);
        bits = GLS_DEFAULT;
    } else {
        GL_ForceTexture(TMU_TEXTURE, TEXNUM_PP_SCENE);
        GL_ForceTexture(TMU_LIGHTMAP, TEXNUM_PP_BLUR_0);
        if (waterwarp)
            bits |= GLS_WARP_ENABLE;
    }

    // upscale & add
    qglBindFramebuffer(GL_FRAMEBUFFER, 0);
    GL_PostProcess(bits, glr.fd.x, glr.fd.y, glr.fd.width, glr.fd.height);
}

typedef enum {
    PP_NONE      = 0,
    PP_WATERWARP = BIT(0),
    PP_BLOOM     = BIT(1),
} pp_flags_t;

static pp_flags_t GL_BindFramebuffer(void)
{
    pp_flags_t flags = PP_NONE;
    bool resized = false;

    if ((glr.fd.rdflags & RDF_UNDERWATER) && gl_waterwarp->integer)
        flags |= PP_WATERWARP;

    if (!(glr.fd.rdflags & RDF_NOWORLDMODEL) && gl_bloom->integer)
        flags |= PP_BLOOM;

    if (flags)
        resized = glr.fd.width != glr.framebuffer_width || glr.fd.height != glr.framebuffer_height;

    if (resized || gl_waterwarp->modified || gl_bloom->modified) {
        glr.framebuffer_ok     = GL_InitFramebuffers();
        glr.framebuffer_width  = glr.fd.width;
        glr.framebuffer_height = glr.fd.height;
        gl_waterwarp->modified = false;
        gl_bloom->modified     = false;
        if (gl_bloom->integer)
            GL_ShaderUpdateBlur();
    }

    if (!flags || !glr.framebuffer_ok)
        return PP_NONE;

    qglBindFramebuffer(GL_FRAMEBUFFER, FBO_SCENE);
    glr.framebuffer_bound = true;

    if (gl_clear->integer) {
        if (flags & PP_BLOOM) {
            static const GLenum buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
            static const vec4_t black = { 0, 0, 0, 1 };
            qglDrawBuffers(2, buffers);
            qglClearBufferfv(GL_COLOR, 0, gl_static.clearcolor);
            qglClearBufferfv(GL_COLOR, 1, black);
            qglDrawBuffers(1, buffers);
        } else {
            qglClear(GL_COLOR_BUFFER_BIT);
        }
    }

    return flags;
}

static void GL_SetupFog(void)
{
    if (!gl_fog->integer)
        return;

    if (glr.fd.fog.density > 0)
        glr.fog_bits |= GLS_FOG_GLOBAL;
    if (glr.fd.heightfog.density > 0 && glr.fd.heightfog.falloff > 0)
        glr.fog_bits |= GLS_FOG_HEIGHT;
    if (glr.fd.fog.sky_factor > 0)
        glr.fog_bits_sky |= GLS_FOG_SKY;

    if (!(glr.fog_bits | glr.fog_bits_sky))
        return;

    VectorCopy(glr.fd.fog.color, gls.u_block.fog_color);
    gls.u_block.fog_color[3] = glr.fd.fog.density / 64;
    gls.u_block.fog_sky_factor = glr.fd.fog.sky_factor;

    VectorCopy(glr.fd.heightfog.start.color, gls.u_block.heightfog_start);
    gls.u_block.heightfog_start[3] = glr.fd.heightfog.start.dist;

    VectorCopy(glr.fd.heightfog.end.color, gls.u_block.heightfog_end);
    gls.u_block.heightfog_end[3] = glr.fd.heightfog.end.dist;

    gls.u_block.heightfog_density = glr.fd.heightfog.density;
    gls.u_block.heightfog_falloff = glr.fd.heightfog.falloff;

    gls.u_block_dirty |= DIRTY_BLOCK;
}

void R_RenderFrame(const refdef_t *fd)
{
    GL_Flush2D();

    Q_assert(gl_static.world.cache || (fd->rdflags & RDF_NOWORLDMODEL));

    GL_SortEntities();

    glr.fog_bits = glr.fog_bits_sky = 0;

    GL_DrawShadowMap(fd);

    glr.drawframe++;

    glr.fd = *fd;

    pp_flags_t pp_flags = GL_BindFramebuffer();

    GL_Setup3D();

    if (glr.fd.rdflags & RDF_NOWORLDMODEL)
        GL_SetupFrustum(2048);
    else
        GL_SetupFrustum(gl_static.world.size * 2);

    GL_SetupFog();

    GL_DrawWorld();

    GL_DrawEntities(glr.ents.bmodels, RF_VIEWERMODEL);

    GL_DrawEntities(glr.ents.opaque, RF_VIEWERMODEL);

    GL_DrawEntities(glr.ents.alpha_back, RF_VIEWERMODEL);

    GL_DrawAlphaFaces();

    memcpy(gls.u_block.m_model, gl_identity, sizeof(gls.u_block.m_model));

    GL_DrawBeams();

    GL_DrawParticles();

    GL_OccludeFlares();

    GL_DrawFlares();

    GL_DrawEntities(glr.ents.alpha_front, RF_VIEWERMODEL);

    GL_DrawDebugObjects();

    if (glr.framebuffer_bound) {
        qglBindFramebuffer(GL_FRAMEBUFFER, 0);
        glr.framebuffer_bound = false;
    }

    // go back into 2D mode
    GL_Setup2D();

    if (pp_flags & PP_BLOOM) {
        GL_DrawBloom(pp_flags & PP_WATERWARP);
    } else if (pp_flags & PP_WATERWARP) {
        GL_ForceTexture(TMU_TEXTURE, TEXNUM_PP_SCENE);
        GL_PostProcess(GLS_WARP_ENABLE, glr.fd.x, glr.fd.y, glr.fd.width, glr.fd.height);
    }

    if (gl_polyblend->integer)
        GL_Blend();

#if USE_DEBUG
    if (gl_lightmap->integer > 1)
        Draw_Lightmaps();
#endif

    if (gl_showerrors->integer > 1)
        GL_ShowErrors(__func__);
}

void R_BeginFrame(void)
{
    memset(&c, 0, sizeof(c));

    if (gl_finish->integer)
        qglFinish();

    GL_Setup2D();

    if (gl_clear->integer)
        qglClear(GL_COLOR_BUFFER_BIT);

    if (gl_showerrors->integer > 1)
        GL_ShowErrors(__func__);
}

void R_EndFrame(void)
{
    extern cvar_t *cl_async;

#if USE_DEBUG
    if (gl_showstats->integer) {
        GL_Flush2D();
        Draw_Stats();
    }
    if (gl_showscrap->integer)
        Draw_Scrap();
#endif
    GL_Flush2D();

    if (gl_showtearing->integer)
        GL_DrawTearing();

    if (gl_showerrors->integer > 1)
        GL_ShowErrors(__func__);

    vid->swap_buffers();

    if (qglFenceSync && cl_async->integer > 1 && !gl_static.sync)
        gl_static.sync = qglFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

bool R_VideoSync(void)
{
    if (!gl_static.sync)
        return true;

    if (qglClientWaitSync(gl_static.sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0) == GL_TIMEOUT_EXPIRED)
        return false;

    qglDeleteSync(gl_static.sync);
    gl_static.sync = 0;

    return true;
}

// ==============================================================================

static void GL_Strings_f(void)
{
    GLint integer = 0;
    GLfloat value = 0;

    Com_Printf("GL_VENDOR: %s\n", qglGetString(GL_VENDOR));
    Com_Printf("GL_RENDERER: %s\n", qglGetString(GL_RENDERER));
    Com_Printf("GL_VERSION: %s\n", qglGetString(GL_VERSION));

    if (gl_config.ver_sl) {
        Com_Printf("GL_SHADING_LANGUAGE_VERSION: %s\n", qglGetString(GL_SHADING_LANGUAGE_VERSION));
    }

    if (Cmd_Argc() > 1) {
        Com_Printf("GL_EXTENSIONS: ");
        if (qglGetStringi) {
            qglGetIntegerv(GL_NUM_EXTENSIONS, &integer);
            for (int i = 0; i < integer; i++)
                Com_Printf("%s ", qglGetStringi(GL_EXTENSIONS, i));
        } else {
            const char *s = (const char *)qglGetString(GL_EXTENSIONS);
            if (s) {
                while (*s) {
                    Com_Printf("%s", s);
                    s += min(strlen(s), MAXPRINTMSG - 1);
                }
            }
        }
        Com_Printf("\n");
    }

    qglGetIntegerv(GL_MAX_TEXTURE_SIZE, &integer);
    Com_Printf("GL_MAX_TEXTURE_SIZE: %d\n", integer);

    if (gl_config.caps & QGL_CAP_TEXTURE_ANISOTROPY) {
        qglGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &value);
        Com_Printf("GL_MAX_TEXTURE_MAX_ANISOTROPY: %.f\n", value);
    }

    Com_Printf("GL_PFD: color(%d-bit) Z(%d-bit) stencil(%d-bit)\n",
               gl_config.colorbits, gl_config.depthbits, gl_config.stencilbits);
}

#if USE_DEBUG

static size_t GL_ViewCluster_m(char *buffer, size_t size)
{
    return Q_snprintf(buffer, size, "%d", glr.viewcluster1);
}

static size_t GL_ViewLeaf_m(char *buffer, size_t size)
{
    const bsp_t *bsp = gl_static.world.cache;

    if (bsp) {
        const mleaf_t *leaf = BSP_PointLeaf(bsp->nodes, glr.fd.vieworg);
        return Q_snprintf(buffer, size, "%td %d %d %d %#x", leaf - bsp->leafs,
                          leaf->cluster, leaf->numleafbrushes, leaf->numleaffaces,
                          leaf->contents);
    }

    return Q_strlcpy(buffer, "", size);
}

#endif

// ugly hack to reset sky
static void gl_drawsky_changed(cvar_t *self)
{
    Cbuf_AddText(&cmd_buffer, "sky \"\"\n");
}

static void gl_novis_changed(cvar_t *self)
{
    glr.viewcluster1 = glr.viewcluster2 = -2;
}

static void gl_swapinterval_changed(cvar_t *self)
{
    if (vid && vid->swap_interval)
        vid->swap_interval(self->integer);
}

static void gl_clearcolor_changed(cvar_t *self)
{
    color_t color;

    if (!COM_ParseColor(self->string, &color)) {
        Com_WPrintf("Invalid value '%s' for '%s'\n", self->string, self->name);
        Cvar_Reset(self);
        color.u32 = U32_BLACK;
    }

    gl_static.clearcolor[0] = color.u8[0] / 255.0f;
    gl_static.clearcolor[1] = color.u8[1] / 255.0f;
    gl_static.clearcolor[2] = color.u8[2] / 255.0f;
    gl_static.clearcolor[3] = color.u8[3] / 255.0f;

    if (qglClearColor)
        qglClearColor(Vector4Unpack(gl_static.clearcolor));
}

static void gl_bloom_sigma_changed(cvar_t *self)
{
    if (gl_static.programs && gl_bloom->integer)
        GL_ShaderUpdateBlur();
}

static void GL_Register(void)
{
    // regular variables
    gl_partscale = Cvar_Get("gl_partscale", "2", 0);
    gl_partstyle = Cvar_Get("gl_partstyle", "0", 0);
    gl_beamstyle = Cvar_Get("gl_beamstyle", "1", 0);
    gl_celshading = Cvar_Get("gl_celshading", "0", 0);
    gl_dotshading = Cvar_Get("gl_dotshading", "0", 0);
    gl_shadowmap = Cvar_Get("gl_shadowmap", "1", 0);
    gl_shadows = Cvar_Get("gl_shadows", "0", CVAR_ARCHIVE);
    gl_modulate = Cvar_Get("gl_modulate", "2", CVAR_ARCHIVE);
    gl_modulate_world = Cvar_Get("gl_modulate_world", "1", 0);
    gl_modulate_entities = Cvar_Get("gl_modulate_entities", "1", 0);
    gl_dynamic = Cvar_Get("gl_dynamic", "1", 0);
    gl_flarespeed = Cvar_Get("gl_flarespeed", "8", 0);
    gl_fontshadow = Cvar_Get("gl_fontshadow", "0", 0);
#if USE_MD5
    gl_md5_load = Cvar_Get("gl_md5_load", "1", CVAR_FILES);
    gl_md5_use = Cvar_Get("gl_md5_use", "1", 0);
    gl_md5_distance = Cvar_Get("gl_md5_distance", "2048", 0);
#endif
    gl_damageblend_frac = Cvar_Get("gl_damageblend_frac", "0.2", 0);
    gl_waterwarp = Cvar_Get("gl_waterwarp", "1", 0);
    gl_fog = Cvar_Get("gl_fog", "1", 0);
    gl_bloom = Cvar_Get("gl_bloom", "1", 0);
    gl_bloom_sigma = Cvar_Get("gl_bloom_sigma", "4", 0);
    gl_bloom_sigma->changed = gl_bloom_sigma_changed;
    gl_swapinterval = Cvar_Get("gl_swapinterval", "1", CVAR_ARCHIVE);
    gl_swapinterval->changed = gl_swapinterval_changed;

    // development variables
    gl_znear = Cvar_Get("gl_znear", "2", CVAR_CHEAT);
    gl_drawworld = Cvar_Get("gl_drawworld", "1", CVAR_CHEAT);
    gl_drawentities = Cvar_Get("gl_drawentities", "1", CVAR_CHEAT);
    gl_drawsky = Cvar_Get("gl_drawsky", "1", 0);
    gl_drawsky->changed = gl_drawsky_changed;
    gl_draworder = Cvar_Get("gl_draworder", "1", 0);
    gl_showtris = Cvar_Get("gl_showtris", "0", CVAR_CHEAT);
    gl_showorigins = Cvar_Get("gl_showorigins", "0", CVAR_CHEAT);
    gl_showtearing = Cvar_Get("gl_showtearing", "0", CVAR_CHEAT);
    gl_showbloom = Cvar_Get("gl_showbloom", "0", CVAR_CHEAT);
#if USE_DEBUG
    gl_showstats = Cvar_Get("gl_showstats", "0", 0);
    gl_showscrap = Cvar_Get("gl_showscrap", "0", 0);
    gl_nobind = Cvar_Get("gl_nobind", "0", CVAR_CHEAT);
    gl_test = Cvar_Get("gl_test", "0", 0);
#endif
    gl_cull_nodes = Cvar_Get("gl_cull_nodes", "1", 0);
    gl_cull_models = Cvar_Get("gl_cull_models", "1", 0);
    gl_clear = Cvar_Get("gl_clear", "0", 0);
    gl_clearcolor = Cvar_Get("gl_clearcolor", "black", 0);
    gl_clearcolor->changed = gl_clearcolor_changed;
    gl_clearcolor->generator = Com_Color_g;
    gl_finish = Cvar_Get("gl_finish", "0", 0);
    gl_novis = Cvar_Get("gl_novis", "0", 0);
    gl_novis->changed = gl_novis_changed;
    gl_lockpvs = Cvar_Get("gl_lockpvs", "0", CVAR_CHEAT);
    gl_lightmap = Cvar_Get("gl_lightmap", "0", CVAR_CHEAT);
    gl_lightmap_bits = Cvar_Get("gl_lightmap_bits", "0", CVAR_FILES);
    gl_fullbright = Cvar_Get("gl_fullbright", "0", CVAR_CHEAT);
    gl_lightgrid = Cvar_Get("gl_lightgrid", "1", 0);
    gl_polyblend = Cvar_Get("gl_polyblend", "1", 0);
    gl_showerrors = Cvar_Get("gl_showerrors", "1", 0);

    gl_swapinterval_changed(gl_swapinterval);
    gl_clearcolor_changed(gl_clearcolor);

    Cmd_AddCommand("strings", GL_Strings_f);

#if USE_DEBUG
    Cmd_AddMacro("gl_viewcluster", GL_ViewCluster_m);
    Cmd_AddMacro("gl_viewleaf", GL_ViewLeaf_m);
#endif
}

static void GL_Unregister(void)
{
    Cmd_RemoveCommand("strings");
}

static void APIENTRY myDebugProc(GLenum source, GLenum type, GLuint id, GLenum severity,
                                 GLsizei length, const GLchar *message, const void *userParam)
{
    int level = PRINT_DEVELOPER;

    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:   level = PRINT_ERROR;   break;
        case GL_DEBUG_SEVERITY_MEDIUM: level = PRINT_WARNING; break;
        case GL_DEBUG_SEVERITY_LOW:    level = PRINT_ALL;     break;
    }

    Com_LPrintf(level, "%s\n", message);
}

static void GL_SetupConfig(void)
{
    GLint integer = 0;

    qglGetIntegerv(GL_MAX_TEXTURE_SIZE, &integer);
    gl_config.max_texture_size_log2 = Q_log2(min(integer, MAX_TEXTURE_SIZE));
    gl_config.max_texture_size = 1U << gl_config.max_texture_size_log2;

    if (gl_config.caps & QGL_CAP_CLIENT_VA) {
        qglGetIntegerv(GL_RED_BITS, &integer);
        gl_config.colorbits = integer;
        qglGetIntegerv(GL_GREEN_BITS, &integer);
        gl_config.colorbits += integer;
        qglGetIntegerv(GL_BLUE_BITS, &integer);
        gl_config.colorbits += integer;

        qglGetIntegerv(GL_DEPTH_BITS, &integer);
        gl_config.depthbits = integer;

        qglGetIntegerv(GL_STENCIL_BITS, &integer);
        gl_config.stencilbits = integer;
    } else if (qglGetFramebufferAttachmentParameteriv) {
        GLenum backbuf = gl_config.ver_es ? GL_BACK : GL_BACK_LEFT;

        qglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, backbuf, GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE, &integer);
        gl_config.colorbits = integer;
        qglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, backbuf, GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE, &integer);
        gl_config.colorbits += integer;
        qglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, backbuf, GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE, &integer);
        gl_config.colorbits += integer;

        qglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &integer);
        gl_config.depthbits = integer;

        qglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_STENCIL, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &integer);
        gl_config.stencilbits = integer;
    }

    if (qglDebugMessageCallback && qglIsEnabled(GL_DEBUG_OUTPUT)) {
        Com_Printf("Enabling GL debug output.\n");
        qglEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        if (Cvar_VariableInteger("gl_debug") < 2)
            qglDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
        qglDebugMessageCallback(myDebugProc, NULL);
    }

    if (gl_config.caps & QGL_CAP_SHADER_STORAGE) {
        integer = 0;
        qglGetIntegerv(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, &integer);
        if (integer < 2) {
            Com_DPrintf("Not enough shader storage blocks available\n");
            gl_config.caps &= ~QGL_CAP_SHADER_STORAGE;
        } else {
            integer = 1;
            qglGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &integer);
            if (integer & (integer - 1))
                integer = Q_npot32(integer);
            Com_DPrintf("SSBO alignment: %d\n", integer);
            gl_config.ssbo_align = integer;
        }
    }

    if (gl_config.caps & QGL_CAP_BUFFER_TEXTURE) {
        integer = 0;
        qglGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &integer);
        if (integer < MOD_MAXSIZE_GPU) {
            Com_DPrintf("Not enough buffer texture size available\n");
            gl_config.caps &= ~QGL_CAP_BUFFER_TEXTURE;
        }
    }

    GL_ShowErrors(__func__);
}

static void GL_InitTables(void)
{
    for (int i = 0; i < NUMVERTEXNORMALS; i++) {
        const vec_t *v = bytedirs[i];
        float lat = acosf(v[2]);
        float lng = atan2f(v[1], v[0]);

        gl_static.latlngtab[i][0] = (int)(lat * (255 / (2 * M_PIf))) & 255;
        gl_static.latlngtab[i][1] = (int)(lng * (255 / (2 * M_PIf))) & 255;
    }

    for (int i = 0; i < 256; i++)
        gl_static.sintab[i] = sinf(i * (2 * M_PIf / 255));
}

static void GL_PostInit(void)
{
    r_registration_sequence = 1;

    GL_InitImages();
    GL_InitQueries();
    MOD_Init();
}

void GL_InitQueries(void)
{
    if (!qglBeginQuery)
        return;

    gl_static.samples_passed = GL_SAMPLES_PASSED;
    if (gl_config.ver_gl >= QGL_VER(3, 3) || gl_config.ver_es >= QGL_VER(3, 0))
        gl_static.samples_passed = GL_ANY_SAMPLES_PASSED;

    Q_assert(!gl_static.queries);
    gl_static.queries = HashMap_TagCreate(int, glquery_t, HashInt32, NULL, TAG_RENDERER);
}

void GL_DeleteQueries(void)
{
    if (!gl_static.queries)
        return;

    uint32_t map_size = HashMap_Size(gl_static.queries);
    for (int i = 0; i < map_size; i++) {
        glquery_t *q = HashMap_GetValue(glquery_t, gl_static.queries, i);
        qglDeleteQueries(1, &q->query);
    }

    if (map_size)
        Com_DPrintf("%s: %u queries deleted\n", __func__, map_size);

    HashMap_Destroy(gl_static.queries);
    gl_static.queries = NULL;
}

// ==============================================================================

/*
===============
R_Init
===============
*/
bool R_Init(bool total)
{
    Com_DPrintf("GL_Init( %i )\n", total);

    if (!total) {
        GL_PostInit();
        return true;
    }

    Com_Printf("------- R_Init -------\n");
    Com_Printf("Using video driver: %s\n", vid->name);

    // initialize OS-specific parts of OpenGL
    // create the window and set up the context
    if (!vid->init())
        return false;

    // initialize our QGL dynamic bindings
    if (!QGL_Init())
        goto fail;

    // get various limits from OpenGL
    GL_SetupConfig();

    // register our variables
    GL_Register();

    GL_InitArrays();

    GL_InitShaders();

    GL_InitState();

    GL_InitTables();

    GL_InitDebugDraw();

    GL_PostInit();

    GL_ShowErrors(__func__);

    Com_Printf("----------------------\n");

    return true;

fail:
    memset(&gl_static, 0, sizeof(gl_static));
    memset(&gl_config, 0, sizeof(gl_config));
    QGL_Shutdown();
    vid->shutdown();
    return false;
}

/*
===============
R_Shutdown
===============
*/
void R_Shutdown(bool total)
{
    Com_DPrintf("GL_Shutdown( %i )\n", total);

    GL_FreeWorld();
    GL_DeleteQueries();
    GL_ShutdownImages();
    MOD_Shutdown();

    if (!total)
        return;

    if (gl_static.sync) {
        qglDeleteSync(gl_static.sync);
        gl_static.sync = 0;
    }

    GL_ShutdownDebugDraw();

    GL_ShutdownState();

    GL_ShutdownShaders();

    GL_ShutdownArrays();

    // shutdown our QGL subsystem
    QGL_Shutdown();

    // shut down OS specific OpenGL stuff like contexts, etc.
    vid->shutdown();

    GL_Unregister();

    memset(&gl_static, 0, sizeof(gl_static));
    memset(&gl_config, 0, sizeof(gl_config));
}

/*
===============
R_GetGLConfig
===============
*/
r_opengl_config_t R_GetGLConfig(void)
{
#define GET_CVAR(name, def, min, max) \
    Cvar_ClampInteger(Cvar_Get(name, def, CVAR_REFRESH), min, max)

    r_opengl_config_t cfg = {
        .colorbits    = GET_CVAR("gl_colorbits",    "0", 0, 32),
        .depthbits    = GET_CVAR("gl_depthbits",    "0", 0, 32),
        .stencilbits  = GET_CVAR("gl_stencilbits",  "8", 0,  8),
        .multisamples = GET_CVAR("gl_multisamples", "0", 0, 32),
        .debug        = GET_CVAR("gl_debug",        "0", 0,  2),
    };

    if (cfg.colorbits == 0)
        cfg.colorbits = 24;

    if (cfg.depthbits == 0)
        cfg.depthbits = cfg.colorbits > 16 ? 24 : 16;

    if (cfg.depthbits < 24)
        cfg.stencilbits = 0;

    if (cfg.multisamples < 2)
        cfg.multisamples = 0;

    const char *s = Cvar_Get("gl_profile", DEFGLPROFILE, CVAR_REFRESH)->string;

    if (!Q_stricmpn(s, "gl", 2))
        cfg.profile = QGL_PROFILE_CORE;
    else if (!Q_stricmpn(s, "es", 2))
        cfg.profile = QGL_PROFILE_ES;

    if (cfg.profile) {
        int major = 0, minor = 0;

        sscanf(s + 2, "%d.%d", &major, &minor);
        if (major >= 1 && minor >= 0) {
            cfg.major_ver = major;
            cfg.minor_ver = minor;
        } else if (cfg.profile == QGL_PROFILE_CORE) {
            cfg.major_ver = 3;
            cfg.minor_ver = 2;
        } else if (cfg.profile == QGL_PROFILE_ES) {
            cfg.major_ver = 3;
            cfg.minor_ver = 0;
        }
    }

    return cfg;
}

/*
===============
R_BeginRegistration
===============
*/
void R_BeginRegistration(const char *name)
{
    gl_static.registering = true;
    r_registration_sequence++;

    memset(&glr, 0, sizeof(glr));
    glr.viewcluster1 = glr.viewcluster2 = -2;

    gl_shadowmap->modified = true;

    GL_LoadWorld(name);
}

/*
===============
R_EndRegistration
===============
*/
void R_EndRegistration(void)
{
    IMG_FreeUnused();
    MOD_FreeUnused();
    Scrap_Upload();
    gl_static.registering = false;
}

/*
===============
R_ModeChanged
===============
*/
void R_ModeChanged(int width, int height, int flags)
{
    if (qglFenceSync)
        flags |= QVF_VIDEOSYNC;

    r_config.width = width;
    r_config.height = height;
    r_config.flags = flags;
    r_config.scale = R_ClampScale(NULL);
}

/*
===============
R_ClearScene
===============
*/
void R_ClearScene(void)
{
    r_numdlights = 0;
    r_numentities = 0;
    r_numparticles = 0;
    r_particles = NULL;
}

/*
===============
R_AddEntity
===============
*/
void R_AddEntity(const entity_t *ent)
{
    glentity_t  *glent;

    if (r_numentities >= MAX_ENTITIES)
        return;
    glent = &r_entities[r_numentities++];
    glent->ent_ = *ent;
    glent->next = NULL;
}

/*
===============
R_AddLight
===============
*/
void R_AddLight(const light_t *light)
{
    dlight_t    *dl;

    if (r_numdlights >= MAX_DLIGHTS)
        return;
    if (gl_dynamic->integer != 1)
        return;
    if (VectorLengthSquared(light->color) < 0.001f)
        return;
    if (light->radius < 1.0f)
        return;

    dl = &r_dlights[r_numdlights++];
    VectorCopy(light->origin, dl->origin);
    dl->radius = light->radius;
    VectorCopy(light->color, dl->color);
    if (VectorEmpty(light->dir) || light->cone_angle == 0.0f) {
        dl->sphere = true;
        VectorClear(dl->dir);
        dl->cone = 0.0f;
    } else {
        dl->sphere = false;
        VectorCopy(light->dir, dl->dir);
        dl->cone = cosf(DEG2RAD(light->cone_angle));
    }
    if (light->resolution > 1)
        dl->resolution = light->resolution;
    else
        dl->resolution = 512;
    dl->flags = light->flags;
    dl->key = light->key;
    if (light->color[0] < 0 || light->color[1] < 0 || light->color[2] < 0)
        dl->flags |= RF_NOSHADOW;
}

/*
===============
R_SetLightStyle
===============
*/
void R_SetLightStyle(unsigned style, float value)
{
    if (style >= 255)
        return;

    if (gl_dynamic->integer <= 0)
        value = 1.0f;
    else if (gl_dynamic->integer >= 2 && style < 32)
        value = 1.0f;

    gls.u_styles.styles[style][0] = value;
}

/*
===============
R_LocateParticles
===============
*/
void R_LocateParticles(const particle_t *p, int count)
{
    r_particles = p;
    r_numparticles = count;
}
