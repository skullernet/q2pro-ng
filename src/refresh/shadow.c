/*
Copyright (C) 2025 Andrey Nazarov

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

static const vec3_t shadowdirs[6] = {
    {-1, 0, 0 }, { 0,-1, 0 }, { 0, 0,-1 },
    { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }
};

static dlight_t *GL_FindStaticLight(int key)
{
    if (!key)
        return NULL;

    for (int i = 0; i < glr.num_static_lights; i++)
        if (glr.static_lights[i].key == key)
            return &glr.static_lights[i];

    return NULL;
}

static dlight_t *GL_AllocStaticLight(void)
{
    for (int i = 0; i < glr.num_static_lights; i++)
        if (!glr.static_lights[i].key)
            return &glr.static_lights[i];

    if (glr.num_static_lights == MAX_STATIC_LIGHTS)
        return NULL;

    return &glr.static_lights[glr.num_static_lights++];
}

static bool GL_CompareLights(const dlight_t *a, const dlight_t *b)
{
    return VectorCompare(a->origin, b->origin) && a->radius == b->radius
        && VectorCompare(a->dir, b->dir) && a->cone == b->cone && a->resolution == b->resolution;
}

static bool GL_DrawStaticShadowView(const dlight_t *light, const vec3_t dir, float fov)
{
    const int size = gl_config.max_texture_size;
    const int res = light->resolution;
    int s, t;

    if (glr.num_static_shadow_views == MAX_SHADOW_VIEWS)
        return false;

    if (!GL_AllocBlock(size, size, glr.static_shadow_inuse, res, res, &s, &t))
        return false;

    shadow_view_t *view = &glr.static_shadow_views[glr.num_static_shadow_views++];
    view->s = s;
    view->t = t;

    vectoangles(dir, glr.fd.viewangles);

    Matrix_Frustum(fov, fov, 1.0f, light->radius, gls.proj_matrix);
    glr.fd.fov_x = glr.fd.fov_y = fov;

    GL_RotateForViewer();

    qglViewport(s, t, res, res);

    glr.drawframe++;

    GL_SetupFrustum(light->radius);

    GL_DrawWorld();

    return true;
}

static void GL_ClearStaticShadows(void)
{
    memset(glr.static_shadow_inuse, 0, sizeof(glr.static_shadow_inuse));
    glr.num_static_lights = 0;
    glr.num_static_shadow_views = 0;
}

static void GL_DrawStaticShadows(void)
{
    for (int i = 0; i < r_numdlights; i++) {
        const dlight_t *light = &r_dlights[i];

        if (!light->key || (light->flags & RF_NOSHADOW))
            continue;

        if (!glr.num_static_lights)
            qglClear(GL_DEPTH_BUFFER_BIT);

        dlight_t *cache = GL_FindStaticLight(light->key);
        if (cache) {
            if (GL_CompareLights(cache, light))
                continue;
            // TODO: reuse allocated area?
        } else {
            cache = GL_AllocStaticLight();
            if (!cache)
                break;
        }

        *cache = *light;

        VectorCopy(cache->origin, glr.fd.vieworg);
        VectorCopy(cache->origin, gls.u_block.vieworg);

        cache->firstview = glr.num_static_shadow_views;
        bool ok = true;

        if (cache->sphere) {
            for (int j = 0; j < 6 && ok; j++)
                ok = GL_DrawStaticShadowView(cache, shadowdirs[j], 90.0f);
        } else {
            ok = GL_DrawStaticShadowView(cache, cache->dir, RAD2DEG(acosf(cache->cone)) * 2);
        }

        // TODO: drop old caches?
        if (!ok) {
            memset(cache, 0, sizeof(*cache));
            break;
        }
    }
}

static bool GL_DrawShadowView(const dlight_t *light, const vec3_t dir, float fov, int face)
{
    const int size = gl_config.max_texture_size;
    const int res = light->resolution;
    const float scale = 1.0f / size;
    int s, t;

    if (glr.num_shadow_views == MAX_SHADOW_VIEWS)
        return false;

    if (!GL_AllocBlock(size, size, glr.shadow_inuse, res, res, &s, &t))
        return false;

    glShadowView_t *view = &glr.shadow_views[glr.num_shadow_views++];
    view->offset[0] = (res - 2) * scale;
    view->offset[1] = (res - 2) * scale;
    view->offset[2] = (s + 0.5f) * scale;
    view->offset[3] = (t + 0.5f) * scale;

    vectoangles(dir, glr.fd.viewangles);

    Matrix_Frustum(fov, fov, 1.0f, light->radius, gls.proj_matrix);
    glr.fd.fov_x = glr.fd.fov_y = fov;

    GL_RotateForViewer();

    GL_MultMatrix(view->matrix, gls.proj_matrix, gls.view_matrix);

    qglViewport(s, t, res, res);

    glr.drawframe++;

    GL_SetupFrustum(light->radius);

    const dlight_t *cache = GL_FindStaticLight(light->key);
    if (cache) {
        const shadow_view_t *cv = &glr.static_shadow_views[cache->firstview + face];
        qglBlitFramebuffer(cv->s, cv->t, cv->s + res, cv->t + res,
                           s, t, s + res, t + res, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    } else {
        GL_DrawWorld();
    }

    const int exclude = RF_WEAPONMODEL | RF_FULLBRIGHT |
        RF_NOSHADOW | (light->flags & RF_VIEWERMODEL);

    GL_DrawEntities(glr.ents.bmodels, exclude);
    GL_DrawEntities(glr.ents.opaque, exclude);

    return true;
}

void GL_DrawShadowMap(const refdef_t *fd)
{
    if (gl_shadowmap->modified) {
        glr.shadowbuffer_ok = GL_InitShadowBuffer();
        gl_shadowmap->modified = false;
        GL_ClearStaticShadows();
    }

    if (!r_numdlights || (fd->rdflags & RDF_NOWORLDMODEL) || !gl_shadowmap->integer)
        return;

    if (!glr.shadowbuffer_ok)
        return;

    glr.fd = *fd;

    cplane_t frustum[4];
    GL_SetupFrustum(gl_static.world.size * 2);
    memcpy(frustum, glr.frustum, sizeof(frustum));

    qglEnable(GL_POLYGON_OFFSET_FILL);
    qglPolygonOffset(1.5f, 2.0f);

    qglBindFramebuffer(GL_FRAMEBUFFER, FBO_SHADOWMAP_STATIC);
    glr.shadowbuffer_bound = true;

    GL_StateBits(GLS_DEFAULT);
    GL_DrawStaticShadows();

    qglBindFramebuffer(GL_DRAW_FRAMEBUFFER, FBO_SHADOWMAP_DYNAMIC);

    GL_StateBits(GLS_DEFAULT);
    qglClear(GL_DEPTH_BUFFER_BIT);

    glr.num_shadow_views = 0;
    memset(glr.shadow_inuse, 0, sizeof(glr.shadow_inuse));

    for (int i = 0; i < r_numdlights; i++) {
        dlight_t *light = &r_dlights[i];
        int j;

        if (light->flags & RF_NOSHADOW) {
            light->firstview = -1;
            continue;
        }

        for (j = 0; j < 4; j++)
            if (PlaneDiff(light->origin, &frustum[j]) < -light->radius)
                break;
        if (j < 4) {
            light->firstview = -1;
            continue;
        }

        VectorCopy(light->origin, glr.fd.vieworg);
        VectorCopy(light->origin, gls.u_block.vieworg);

        light->firstview = glr.num_shadow_views;
        bool ok = true;

        if (light->sphere) {
            for (j = 0; j < 6 && ok; j++)
                ok = GL_DrawShadowView(light, shadowdirs[j], 90.0f, j);
        } else {
            ok = GL_DrawShadowView(light, light->dir, RAD2DEG(acosf(light->cone)) * 2, 0);
        }

        if (!ok)
            light->firstview = -1;
    }

    qglDisable(GL_POLYGON_OFFSET_FILL);

    qglBindFramebuffer(GL_FRAMEBUFFER, 0);
    glr.shadowbuffer_bound = false;

    GL_BindBuffer(GL_UNIFORM_BUFFER, gl_static.uniform_buffers[UBO_SHADOWVIEWS]);
    qglBufferData(GL_UNIFORM_BUFFER, sizeof(glr.shadow_views), NULL, GL_STREAM_DRAW);
    qglBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glr.shadow_views[0]) * glr.num_shadow_views, glr.shadow_views);
}
