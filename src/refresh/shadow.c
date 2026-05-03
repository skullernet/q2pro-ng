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

static glslight_t *GL_FindStaticLight(int key)
{
    if (!key)
        return NULL;

    glslight_t *cache = glr.static_lights;

    for (int i = 0; i < glr.num_static_lights; i++, cache++)
        if (cache->light.key == key)
            return cache;

    return NULL;
}

static glslight_t *GL_AllocStaticLight(void)
{
    glslight_t *cache = glr.static_lights;

    for (int i = 0; i < glr.num_static_lights; i++, cache++)
        if (!cache->light.key)
            return cache;

    if (glr.num_static_lights == MAX_STATIC_LIGHTS)
        return NULL;

    glr.num_static_lights++;
    return cache;
}

static int GL_CompareLights(const gldlight_t *a, const gldlight_t *b)
{
    if (a->sphere == b->sphere && a->resolution == b->resolution) {
        return Vec3_IsEqual(a->d.origin, b->d.origin) && a->d.radius == b->d.radius
            && Vec3_IsEqual(a->d.dir, b->d.dir) && a->d.cone == b->d.cone;
    }
    return -1;
}

static quadnode_t *GL_CreateQuadNode(quadtree_t *tree, int depth, int size, int s, int t)
{
    quadnode_t  *node;

    Q_assert(tree->numnodes < q_countof(tree->nodes));
    node = &tree->nodes[tree->numnodes++];

    node->size = size;
    node->s = s;
    node->t = t;
    node->inuse = 0;

    if (depth == SHADOWTREE_DEPTH) {
        node->children[0] = NULL;
        return node;
    }

    size /= 2;
    node->children[0] = GL_CreateQuadNode(tree, depth + 1, size, s, t);
    node->children[1] = GL_CreateQuadNode(tree, depth + 1, size, s + size, t);
    node->children[2] = GL_CreateQuadNode(tree, depth + 1, size, s + size, t + size);
    node->children[3] = GL_CreateQuadNode(tree, depth + 1, size, s, t + size);

    return node;
}

static void GL_CreateShadowNodes(void)
{
    glr.shadow_tree.numnodes = 0;
    if (gl_shadowmap->integer)
        GL_CreateQuadNode(&glr.shadow_tree, 0, gl_config.max_texture_size, 0, 0);
}

static bool GL_QuadNodeInUse(quadnode_t *node, int bit)
{
    if (node->inuse & bit)
        return true;

    if (node->children[0])
        for (int i = 0; i < 4; i++)
            if (GL_QuadNodeInUse(node->children[i], bit))
                return true;

    return false;
}

static quadnode_t *GL_AllocQuadNode(quadnode_t *node, int size, int bit)
{
    quadnode_t *n;

    if (node->size == size) {
        if (GL_QuadNodeInUse(node, bit))
            return NULL;
        node->inuse |= bit;
        return node;
    }

    if (node->children[0] && node->size > size && !(node->inuse & bit))
        for (int i = 0; i < 4; i++)
            if ((n = GL_AllocQuadNode(node->children[i], size, bit)))
                return n;

    return NULL;
}

static void GL_FreeQuadNode(quadnode_t *node, int bit)
{
    node->inuse &= ~bit;
}

static void GL_FreeQuadNodes(quadnode_t **nodes, int bit, int count)
{
    for (int i = 0; i < count; i++) {
        if (nodes[i]) {
            GL_FreeQuadNode(nodes[i], bit);
            nodes[i] = NULL;
        }
    }
}

static bool GL_AllocQuadNodes(quadnode_t *root, int size, int bit, quadnode_t **nodes, int count)
{
    for (int i = 0; i < count; i++) {
        nodes[i] = GL_AllocQuadNode(root, size, bit);
        if (!nodes[i]) {
            GL_FreeQuadNodes(nodes, bit, i);
            return false;
        }
    }

    return true;
}

static bool GL_AllocStaticShadowViews(glslight_t *cache)
{
    return GL_AllocQuadNodes(glr.shadow_tree.nodes, cache->light.resolution,
                             SHADOWMAP_STATIC, cache->nodes, cache->light.sphere ? 6 : 1);
}

static void GL_DrawStaticShadowView(const gldlight_t *light, vec3_t dir, float fov, const quadnode_t *node)
{
    glr.fd.viewangles = vectoangles(dir);

    Matrix_Frustum(fov, fov, 1.0f, light->d.radius, gls.proj_matrix);
    glr.fd.fov_x = glr.fd.fov_y = fov;

    GL_RotateForViewer();

    qglViewport(node->s, node->t, node->size, node->size);

    qglScissor(node->s, node->t, node->size, node->size);
    qglClear(GL_DEPTH_BUFFER_BIT);

    glr.drawframe++;

    GL_SetupFrustum(light->d.radius);

    GL_DrawWorld();
}

static void GL_FreeStaticLight(glslight_t *cache)
{
    GL_FreeQuadNodes(cache->nodes, SHADOWMAP_STATIC, 6);
    cache->light.key = 0;
}

static void GL_FreeStaticLights(void)
{
    for (int i = 0; i < glr.num_static_lights; i++)
        GL_FreeStaticLight(&glr.static_lights[i]);

    glr.num_static_lights = 0;
}

static void GL_PurgeStaticLights(void)
{
    glslight_t *cache;
    const gldlight_t *light;
    int i, j;

    for (i = 0, cache = glr.static_lights; i < glr.num_static_lights; i++, cache++) {
        if (!cache->light.key)
            continue;
        for (j = 0, light = r_dlights; j < r_numdlights; j++, light++) {
            if (light->flags & RF_NOSHADOW)
                continue;
            if (light->key == cache->light.key)
                break;
        }
        if (j == r_numdlights)
            GL_FreeStaticLight(cache);
    }
}

static void GL_DrawStaticShadows(void)
{
    for (int i = 0; i < r_numdlights; i++) {
        const gldlight_t *light = &r_dlights[i];

        if (!light->key || (light->flags & RF_NOSHADOW))
            continue;

        glslight_t *cache = GL_FindStaticLight(light->key);
        if (cache) {
            int res = GL_CompareLights(&cache->light, light);
            if (res > 0) {
                c.staticShadowsCached++;
                continue;
            }
            if (res < 0)
                GL_FreeStaticLight(cache);
        } else {
            cache = GL_AllocStaticLight();
            if (!cache) {
                GL_PurgeStaticLights();
                cache = GL_AllocStaticLight();
                if (!cache) {
                    c.staticShadowsOverrun++;
                    continue;
                }
            }
        }

        cache->light = *light;

        if (!cache->nodes[0]) {
            if (!GL_AllocStaticShadowViews(cache)) {
                GL_PurgeStaticLights();
                if (!GL_AllocStaticShadowViews(cache)) {
                    GL_FreeStaticLight(cache);
                    c.staticShadowsOverrun++;
                    continue;
                }
            }
        }

        glr.fd.vieworg = light->d.origin;
        gls.u_block.vieworg = Vec4_FromVec3(light->d.origin, 0);

        if (light->sphere) {
            for (int j = 0; j < 6; j++)
                GL_DrawStaticShadowView(light, shadowdirs[j], 90.0f, cache->nodes[j]);
        } else {
            GL_DrawStaticShadowView(light, light->d.dir, RAD2DEG(acosf(light->d.cone)) * 2, cache->nodes[0]);
        }

        c.staticShadowsDrawn++;
    }
}

static void GL_DrawShadowView(const gldlight_t *light, vec3_t dir, float fov, int face)
{
    const int res = light->resolution;
    const float scale = 1.0f / gl_config.max_texture_size;
    const quadnode_t *node = glr.shadow_nodes[light->d.firstview + face];
    const int s = node->s;
    const int t = node->t;
    Q_assert(node->size == res);

    glShadowView_t *view = &glr.shadow_views[light->d.firstview + face];
    view->offset.x = (res - 2) * scale;
    view->offset.y = (res - 2) * scale;
    view->offset.z = (s + 0.5f) * scale;
    view->offset.w = (t + 0.5f) * scale;

    glr.fd.viewangles = vectoangles(dir);

    Matrix_Frustum(fov, fov, 1.0f, light->d.radius, gls.proj_matrix);
    glr.fd.fov_x = glr.fd.fov_y = fov;

    GL_RotateForViewer();

    GL_MultMatrix(view->matrix, gls.proj_matrix, gls.view_matrix);

    qglViewport(s, t, res, res);

    glr.drawframe++;

    GL_SetupFrustum(light->d.radius);

    const glslight_t *cache = GL_FindStaticLight(light->key);
    if (cache) {
        node = cache->nodes[face];
        Q_assert(node->size == res);
        qglBlitFramebuffer(node->s, node->t, node->s + res, node->t + res,
                           s, t, s + res, t + res, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    } else {
        GL_DrawWorld();
    }

    for (const glentity_t *ent = glr.ents.shadow; ent; ent = ent->shadow_next) {
        if (ent->e.flags & light->flags & RF_VIEWERMODEL)
            continue;
        GL_DrawEntity(ent);
    }
}

static void GL_DrawDynamicShadows(const cplane_t *frustum)
{
    glr.num_shadow_views = 0;

    for (int i = 0; i < r_numdlights; i++) {
        gldlight_t *light = &r_dlights[i];
        int j, num_views;

        light->d.firstview = -1; // not shadow mapped yet

        if (light->flags & RF_NOSHADOW)
            continue;

        for (j = 0; j < 4; j++)
            if (PlaneDiff(light->d.origin, &frustum[j]) < -light->d.radius)
                break;
        if (j < 4)
            continue;

        num_views = light->sphere ? 6 : 1;
        if (glr.num_shadow_views > MAX_SHADOW_VIEWS - num_views)
            continue;

        if (!GL_AllocQuadNodes(glr.shadow_tree.nodes, light->resolution, SHADOWMAP_DYNAMIC,
                               &glr.shadow_nodes[glr.num_shadow_views], num_views))
            continue;

        glr.fd.vieworg = light->d.origin;
        gls.u_block.vieworg = Vec4_FromVec3(light->d.origin, 0);

        light->d.firstview = glr.num_shadow_views;
        glr.num_shadow_views += num_views;

        if (light->sphere) {
            for (j = 0; j < 6; j++)
                GL_DrawShadowView(light, shadowdirs[j], 90.0f, j);
        } else {
            GL_DrawShadowView(light, light->d.dir, RAD2DEG(acosf(light->d.cone)) * 2, 0);
        }
    }

    GL_FreeQuadNodes(glr.shadow_nodes, SHADOWMAP_DYNAMIC, glr.num_shadow_views);
}

void GL_DrawShadowMap(const refdef_t *fd)
{
    if (gl_shadowmap->modified) {
        glr.shadowbuffer_ok = GL_InitShadowBuffer();
        gl_shadowmap->modified = false;
        GL_FreeStaticLights();
        GL_CreateShadowNodes();
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
    qglEnable(GL_SCISSOR_TEST);
    GL_DrawStaticShadows();
    qglDisable(GL_SCISSOR_TEST);

    qglBindFramebuffer(GL_DRAW_FRAMEBUFFER, FBO_SHADOWMAP_DYNAMIC);

    GL_StateBits(GLS_DEFAULT);
    qglClear(GL_DEPTH_BUFFER_BIT);

    GL_DrawDynamicShadows(frustum);

    qglDisable(GL_POLYGON_OFFSET_FILL);

    qglBindFramebuffer(GL_FRAMEBUFFER, 0);
    glr.shadowbuffer_bound = false;

    GL_BindBuffer(GL_UNIFORM_BUFFER, gl_static.uniform_buffers[UBO_SHADOWVIEWS]);
    qglBufferData(GL_UNIFORM_BUFFER, sizeof(glr.shadow_views), NULL, GL_STREAM_DRAW);
    qglBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glr.shadow_views[0]) * glr.num_shadow_views, glr.shadow_views);
}
