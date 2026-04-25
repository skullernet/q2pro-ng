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

static vec3_t GL_SampleLightPoint(void)
{
    const mface_t       *surf = glr.lightpoint.surf;
    const byte          *lightmap;
    const byte          *b1, *b2, *b3, *b4;
    float               fracu, fracv;
    float               w1, w2, w3, w4;
    vec3_t              temp, color;
    int                 s, t, smax, tmax, size;

    s = glr.lightpoint.s;
    t = glr.lightpoint.t;

    fracu = glr.lightpoint.s - s;
    fracv = glr.lightpoint.t - t;

    // compute weights of lightmap blocks
    w1 = (1.0f - fracu) * (1.0f - fracv);
    w2 = fracu * (1.0f - fracv);
    w3 = fracu * fracv;
    w4 = (1.0f - fracu) * fracv;

    smax = surf->lm_width;
    tmax = surf->lm_height;
    size = smax * tmax * 3;

    Q_assert((unsigned)s < smax);
    Q_assert((unsigned)t < tmax);

    color = vec3_origin;

    // add all the lightmaps with bilinear filtering
    lightmap = surf->lightmap;
    for (int i = 0; i < surf->numstyles; i++) {
        b1 = &lightmap[3 * ((t + 0) * smax + (s + 0))];
        b2 = &lightmap[3 * ((t + 0) * smax + (s + 1))];
        b3 = &lightmap[3 * ((t + 1) * smax + (s + 1))];
        b4 = &lightmap[3 * ((t + 1) * smax + (s + 0))];

        temp.r = w1 * b1[0] + w2 * b2[0] + w3 * b3[0] + w4 * b4[0];
        temp.g = w1 * b1[1] + w2 * b2[1] + w3 * b3[1] + w4 * b4[1];
        temp.b = w1 * b1[2] + w2 * b2[2] + w3 * b3[2] + w4 * b4[2];

        color = Vec3_MA(color, gls.u_styles.styles[surf->styles[i]].r, temp);
        lightmap += size;
    }

    return color;
}

static bool GL_LightGridPoint(const lightgrid_t *grid, vec3_t start, vec3_t *color)
{
    vec3_t point, avg;
    uint32_t point_i[3];
    vec3_t samples[8];
    int i, j, mask, numsamples;

    if (!grid->numleafs || !gl_lightgrid->integer)
        return false;

    point = Vec3_Mul(Vec3_Sub(start, grid->mins), grid->scale);
    Vec3_Store(point_i, point);

    avg = vec3_origin;

    for (i = mask = numsamples = 0; i < 8; i++) {
        uint32_t tmp[3];

        tmp[0] = point_i[0] + ((i >> 0) & 1);
        tmp[1] = point_i[1] + ((i >> 1) & 1);
        tmp[2] = point_i[2] + ((i >> 2) & 1);

        const lightgrid_sample_t *s = BSP_LookupLightgrid(grid, tmp);
        if (!s)
            continue;

        samples[i] = vec3_origin;

        for (j = 0; j < grid->numstyles && s->style != 255; j++, s++)
            samples[i] = Vec3_MA(samples[i], gls.u_styles.styles[s->style].r, Vec3_Load(s->rgb));

        // count non-occluded samples
        if (j) {
            mask |= BIT(i);
            avg = Vec3_Add(avg, samples[i]);
            numsamples++;
        }
    }

    if (!mask)
        return false;

    // replace occluded samples with average
    if (mask != 255) {
        avg = Vec3_Scale(avg, 1.0f / numsamples);
        for (i = 0; i < 8; i++)
            if (!(mask & BIT(i)))
                samples[i] = avg;
    }

    // trilinear interpolation
    float fx, fy, fz;
    float bx, by, bz;
    vec3_t lerp_x[4];
    vec3_t lerp_y[2];

    fx = point.x - point_i[0];
    fy = point.y - point_i[1];
    fz = point.z - point_i[2];

    bx = 1.0f - fx;
    by = 1.0f - fy;
    bz = 1.0f - fz;

    lerp_x[0] = Vec3_Mix(samples[0], samples[1], bx, fx);
    lerp_x[1] = Vec3_Mix(samples[2], samples[3], bx, fx);
    lerp_x[2] = Vec3_Mix(samples[4], samples[5], bx, fx);
    lerp_x[3] = Vec3_Mix(samples[6], samples[7], bx, fx);

    lerp_y[0] = Vec3_Mix(lerp_x[0], lerp_x[1], by, fy);
    lerp_y[1] = Vec3_Mix(lerp_x[2], lerp_x[3], by, fy);

    *color = Vec3_Mix(lerp_y[0], lerp_y[1], bz, fz);

    return true;
}

static bool GL_LightPoint(vec3_t start, vec3_t *color)
{
    const bsp_t     *bsp = gl_static.world.cache;
    lightpoint_t    pt;
    vec3_t          end;
    const glentity_t *ent;
    const mmodel_t  *model;
    int             i;

    if (gl_fullbright->integer)
        return false;

    if (!bsp || !bsp->lightmap)
        return false;

    end = start;
    end.z -= 8192;

    // get base lightpoint from world
    BSP_LightPoint(&glr.lightpoint, start, end, bsp->nodes, gl_static.nolm_mask | SURF_TRANS_MASK);

    // trace to other BSP models
    for (i = 0, ent = r_entities; i < r_numentities; i++, ent++) {
        model = ent->bmodel;
        if (!model || !model->numfaces)
            continue;

        // cull in X/Y plane
        if (!Vec3_IsEmpty(ent->angles)) {
            if (fabsf(start.x - ent->origin.x) > model->radius)
                continue;
            if (fabsf(start.y - ent->origin.y) > model->radius)
                continue;
        } else {
            box3_t box = Box3_Translate(model->box, ent->origin);
            if (start.x < box.mins.x || start.x > box.maxs.x)
                continue;
            if (start.y < box.mins.y || start.y > box.maxs.y)
                continue;
        }

        BSP_TransformedLightPoint(&pt, start, end, model->headnode,
                                  gl_static.nolm_mask | SURF_TRANS_MASK, ent->origin, ent->angles);

        if (pt.fraction < glr.lightpoint.fraction)
            glr.lightpoint = pt;
    }

    if (GL_LightGridPoint(&bsp->lightgrid, start, color))
        return true;

    if (glr.lightpoint.surf) {
        *color = GL_SampleLightPoint();
        return true;
    }

    return false;
}

static vec3_t lightorg;
static float lightradius;
static uint64_t lightbit;

static void GL_MarkLights_r(const mnode_t *node)
{
    mface_t *face;
    vec_t dot;
    int i;

    while (node->plane) {
        dot = PlaneDiffFast(lightorg, node->plane);
        if (dot > lightradius) {
            node = node->children[0];
            continue;
        }
        if (dot < -lightradius) {
            node = node->children[1];
            continue;
        }

        for (i = 0, face = node->firstface; i < node->numfaces; i++, face++) {
            if (face->drawflags & gl_static.nolm_mask)
                continue;
            if (face->dlightframe != glr.drawframe) {
                face->dlightframe = glr.drawframe;
                face->dlightbits = 0;
            }
            face->dlightbits |= lightbit;
        }

        GL_MarkLights_r(node->children[0]);
        node = node->children[1];
    }
}

static void GL_MarkLights(void)
{
    for (int i = 0; i < r_numdlights; i++) {
        lightorg = r_dlights[i].origin;
        lightradius = r_dlights[i].radius;
        lightbit = BIT_ULL(i);
        GL_MarkLights_r(gl_static.world.cache->nodes);
    }
}

static void GL_TransformLights(const mmodel_t *model)
{
    for (int i = 0; i < r_numdlights; i++) {
        lightorg = Vec3_Sub(r_dlights[i].origin, glr.ent->origin);
        lightorg = Vec3_Rotate(lightorg, glr.entaxis);
        lightradius = r_dlights[i].radius;
        lightbit = BIT_ULL(i);
        GL_MarkLights_r(model->headnode);
    }
}

void R_LightPoint(vec3_t origin, vec3_t *color)
{
    // get lighting from world
    if (!GL_LightPoint(origin, color)) {
        *color = Vec3(1, 1, 1);
        return;
    }

    float scale = gl_modulate->value * gl_modulate_entities->value / 255.0f;
    *color = Vec3_Scale(*color, scale);
}

static void GL_MarkLeaves(void)
{
    const bsp_t *bsp = gl_static.world.cache;
    const mleaf_t *leaf;
    visrow_t vis1, vis2;
    int i, cluster1, cluster2;
    vec3_t tmp;

    if (gl_lockpvs->integer)
        return;

    leaf = BSP_PointLeaf(bsp->nodes, glr.fd.vieworg);
    cluster1 = cluster2 = leaf->cluster;
    tmp = glr.fd.vieworg;
    if (!leaf->contents)
        tmp.z -= 16;
    else
        tmp.z += 16;
    leaf = BSP_PointLeaf(bsp->nodes, tmp);
    if (!(leaf->contents & CONTENTS_SOLID))
        cluster2 = leaf->cluster;

    if (cluster1 == glr.viewcluster1 && cluster2 == glr.viewcluster2)
        return;

    glr.visframe++;
    glr.viewcluster1 = cluster1;
    glr.viewcluster2 = cluster2;

    if (!bsp->vis || gl_novis->integer || cluster1 == -1) {
        // mark everything visible
        for (i = 0; i < bsp->numnodes; i++)
            bsp->nodes[i].visframe = glr.visframe;

        for (i = 0; i < bsp->numleafs; i++)
            bsp->leafs[i].visframe = glr.visframe;

        glr.nodes_visible = bsp->numnodes;
        return;
    }

    BSP_ClusterVis(bsp, &vis1, cluster1, DVIS_PVS);
    if (cluster1 != cluster2) {
        BSP_ClusterVis(bsp, &vis2, cluster2, DVIS_PVS);
        int longs = VIS_FAST_LONGS(bsp->visrowsize);
        for (i = 0; i < longs; i++)
            vis1.l[i] |= vis2.l[i];
    }

    glr.nodes_visible = 0;
    for (i = 0, leaf = bsp->leafs; i < bsp->numleafs; i++, leaf++) {
        cluster1 = leaf->cluster;
        if (cluster1 == -1)
            continue;
        if (!Q_IsBitSet(vis1.b, cluster1))
            continue;
        // mark parent nodes visible
        for (mnode_t *node = (mnode_t *)leaf; node && node->visframe != glr.visframe; node = node->parent) {
            node->visframe = glr.visframe;
            glr.nodes_visible++;
        }
    }
}

static vec3_t transformed; // vieworg
static mface_t *faces_alpha; // for this submodel only

static inline void GL_DrawSubLeaf(const mleaf_t *leaf)
{
    if (leaf->contents & CONTENTS_SOLID)
        return; // solid leaf

    for (int i = 0; i < leaf->numleaffaces; i++)
        leaf->firstleafface[i]->drawframe = glr.drawframe;

    c.leavesDrawn++;
}

static inline void GL_DrawSubNode(const mnode_t *node)
{
    mface_t *face;
    int i;

    for (i = 0, face = node->firstface; i < node->numfaces; i++, face++) {
        if (face->drawframe != glr.drawframe)
            continue;

        if (face->drawflags & gl_static.nodraw_mask)
            continue;

        if (face->drawflags & SURF_TRANS_MASK) {
            face->next = faces_alpha;
            faces_alpha = face;
        } else
            GL_AddSolidFace(face);
    }

    c.nodesDrawn++;
}

static void GL_SubModelNode_r(const mnode_t *node)
{
    int side;
    vec_t dot;

    while (1) {
        if (!node->plane) {
            GL_DrawSubLeaf((const mleaf_t *)node);
            break;
        }

        dot = PlaneDiffFast(transformed, node->plane);
        side = dot < 0;

        GL_SubModelNode_r(node->children[side]);

        GL_DrawSubNode(node);

        node = node->children[side ^ 1];
    }
}

void GL_DrawBspModel(const mmodel_t *model)
{
    const glentity_t *ent = glr.ent;
    glCullResult_t cull;

    if (!model->numfaces)
        return;

    if (glr.entrotated) {
        cull = GL_CullSphere(ent->origin, model->radius);
        if (cull == CULL_OUT) {
            c.spheresCulled++;
            return;
        }
        if (cull == CULL_CLIP) {
            cull = GL_CullLocalBox(ent->origin, model->box);
            if (cull == CULL_OUT) {
                c.rotatedBoxesCulled++;
                return;
            }
        }
        transformed = Vec3_Sub(glr.fd.vieworg, ent->origin);
        transformed = Vec3_Rotate(transformed, glr.entaxis);
    } else {
        cull = GL_CullBox(Box3_Translate(model->box, ent->origin));
        if (cull == CULL_OUT) {
            c.boxesCulled++;
            return;
        }
        transformed = Vec3_Sub(glr.fd.vieworg, ent->origin);
    }

    if (!glr.shadowbuffer_bound)
        GL_TransformLights(model);

    GL_RotateForEntity();

    GL_BindArrays(VA_3D);

    GL_ClearSolidFaces();

    faces_alpha = NULL;

    GL_SubModelNode_r(model->headnode);

    GL_DrawSolidFaces();

    if (!glr.shadowbuffer_bound)
        for (const mface_t *face = faces_alpha; face; face = face->next)
            GL_DrawFace(face);

    GL_Flush3D();
}

#define NODE_CLIPPED    0
#define NODE_UNCLIPPED  MASK(q_countof(glr.frustum))

static inline bool GL_ClipNode(const mnode_t *node, int *flags)
{
    box_plane_t bits;

    if (*flags == NODE_UNCLIPPED)
        return true;

    for (int i = 0; i < q_countof(glr.frustum); i++) {
        if (*flags & BIT(i))
            continue;
        bits = BoxOnPlaneSide(&node->box, &glr.frustum[i]);
        if (bits == BOX_BEHIND)
            return false;
        if (bits == BOX_INFRONT)
            *flags |= BIT(i);
    }

    return true;
}

static inline void GL_DrawLeaf(const mleaf_t *leaf)
{
    if (leaf->contents & CONTENTS_SOLID)
        return; // solid leaf

    if (!Q_IsBitSet(glr.fd.areabits, leaf->area))
        return; // door blocks sight

    for (int i = 0; i < leaf->numleaffaces; i++)
        leaf->firstleafface[i]->drawframe = glr.drawframe;

    c.leavesDrawn++;
}

static inline void GL_DrawNode(const mnode_t *node)
{
    mface_t *face;
    int i;

    for (i = 0, face = node->firstface; i < node->numfaces; i++, face++) {
        if (face->drawframe != glr.drawframe)
            continue;

        if (face->drawflags & SURF_NODRAW)
            continue;

        if (face->drawflags & SURF_TRANS_MASK)
            GL_AddAlphaFace(face);
        else
            GL_AddSolidFace(face);
    }

    c.nodesDrawn++;
}

static void GL_WorldNode_r(const mnode_t *node, int clipflags)
{
    int side;
    vec_t dot;

    // ignore vis for shadowmapping
    while (node->visframe == glr.visframe || glr.shadowbuffer_bound) {
        if (!GL_ClipNode(node, &clipflags)) {
            c.nodesCulled++;
            break;
        }

        if (!node->plane) {
            GL_DrawLeaf((const mleaf_t *)node);
            break;
        }

        dot = PlaneDiffFast(glr.fd.vieworg, node->plane);
        side = dot < 0;

        GL_WorldNode_r(node->children[side], clipflags);

        GL_DrawNode(node);

        node = node->children[side ^ 1];
    }
}

void GL_DrawWorld(void)
{
    if ((glr.fd.rdflags & RDF_NOWORLDMODEL) || !gl_drawworld->integer)
        return;

    // auto cycle the world frame for texture animation
    gl_world.frame = (int)(glr.fd.time * 2);

    glr.ent = &gl_world;

    if (!glr.shadowbuffer_bound) {
        GL_MarkLeaves();
        GL_MarkLights();
    }

    GL_LoadMatrix(glr.viewmatrix);

    GL_BindArrays(VA_3D);

    GL_ClearSolidFaces();

    GL_WorldNode_r(gl_static.world.cache->nodes,
                   gl_cull_nodes->integer ? NODE_CLIPPED : NODE_UNCLIPPED);

    GL_DrawSolidFaces();

    GL_Flush3D();
}
