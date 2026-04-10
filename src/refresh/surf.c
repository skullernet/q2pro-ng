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
 * gl_surf.c -- surface post-processing code
 *
 */
#include "gl.h"
#include "common/mdfour.h"

lightmap_builder_t lm;

/*
=============================================================================

LIGHTMAPS BUILDING

=============================================================================
*/

#define LM_AllocBlock(w, h, s, t) \
    GL_AllocBlock(lm.block_size, lm.block_size, lm.inuse, w, h, s, t)

static void LM_InitBlock(void)
{
    memset(lm.inuse, 0, sizeof(lm.inuse));
    memset(lm.buffer, 0, MAX_LIGHTMAPS << lm.size_shift);
    lm.numstyles = 0;
}

static void LM_UploadBlock(void)
{
    if (!lm.numstyles)
        return;

    if (lm.nummaps >= LM_MAX_LIGHTMAPS)
        return;

    GL_ForceTextureArray(lm.texnums[lm.nummaps]);
    qglTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA,
                  lm.block_size, lm.block_size, lm.numstyles, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, lm.buffer);
    qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    lm.nummaps++;
}

static void LM_BeginBuilding(void)
{
    const bsp_t *bsp = gl_static.world.cache;
    int bits;

    // lightmap textures are not deleted from memory when changing maps,
    // they are merely reused
    lm.nummaps = 0;

    // use larger lightmaps for DECOUPLED_LM maps
    if (gl_lightmap_bits->integer)
        bits = Cvar_ClampInteger(gl_lightmap_bits, 7, 10);
    else
        bits = 8 + bsp->lm_decoupled * 2;

    // clamp to maximum texture size
    bits = min(bits, gl_config.max_texture_size_log2);

    lm.block_size = 1 << bits;
    lm.block_shift = bits + 2;

    lm.size_shift = bits * 2 + 2;
    lm.buffer = R_Malloc(MAX_LIGHTMAPS << lm.size_shift);

    Com_DPrintf("%s: %d block size\n", __func__, lm.block_size);

    LM_InitBlock();
}

static void LM_EndBuilding(void)
{
    // upload the last lightmap
    LM_UploadBlock();

    Z_Freep(&lm.buffer);

    Com_DPrintf("%s: %d lightmaps built\n", __func__, lm.nummaps);
}

static void LM_BuildSurface(mface_t *surf, vec_t *vbo)
{
    const bsp_t *bsp = gl_static.world.cache;
    int i, j, k, smax, tmax, size, s, t, stride, offset;
    const byte *src;
    byte *out, *dst;
    float scale;

    if (!surf->lightmap)
        return;

    if (surf->drawflags & gl_static.nolm_mask)
        return;

    smax = surf->lm_width;
    tmax = surf->lm_height;

    // validate lightmap extents
    if (smax < 1 || tmax < 1 || smax > lm.block_size || tmax > lm.block_size) {
        Com_WPrintf("Bad lightmap extents: %d x %d\n", smax, tmax);
        surf->lightmap = NULL;  // don't use this lightmap
        return;
    }

    // validate lightmap bounds
    size = smax * tmax;
    offset = surf->lightmap - bsp->lightmap;
    if (surf->numstyles * size * 3 > bsp->numlightmapbytes - offset) {
        Com_WPrintf("Bad surface lightmap\n");
        surf->lightmap = NULL;  // don't use this lightmap
        return;
    }

    if (lm.nummaps >= LM_MAX_LIGHTMAPS)
        return;     // can't have any more

    if (!LM_AllocBlock(smax, tmax, &s, &t)) {
        LM_UploadBlock();
        if (lm.nummaps >= LM_MAX_LIGHTMAPS) {
            Com_EPrintf("Too many lightmaps\n");
            return;
        }
        LM_InitBlock();
        if (!LM_AllocBlock(smax, tmax, &s, &t)) {
            Com_EPrintf("LM_AllocBlock(%d, %d) failed\n", smax, tmax);
            return;
        }
    }

    lm.numstyles = max(lm.numstyles, surf->numstyles);

    // store the surface lightmap parameters
    surf->lm_texnum = lm.texnums[lm.nummaps];

    // build all lightmaps for this surface
    stride = 1 << lm.block_shift;
    offset = (t << lm.block_shift) + (s << 2);

    for (i = 0, src = surf->lightmap; i < surf->numstyles; i++) {
        out = lm.buffer + (i << lm.size_shift) + offset;
        for (j = 0; j < tmax; j++, out += stride) {
            for (k = 0, dst = out; k < smax; k++, src += 3, dst += 4)
                Vec4_Set(dst, src[0], src[1], src[2], 255);
        }
    }

    // normalize and store lightmap texture coordinates in vertices
    scale = 1.0f / lm.block_size;
    for (i = 0; i < surf->numsurfedges; i++) {
        vbo[6] += s + 0.5f;
        vbo[7] += t + 0.5f;
        vbo[6] *= scale;
        vbo[7] *= scale;

        vbo += VERTEX_SIZE;
    }
}

/*
=============================================================================

POLYGONS BUILDING

=============================================================================
*/

static inline double Vec3_Dot64(vec3_t a, vec3_t b) {
    return (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z;
}

static uint32_t color_for_surface(const mface_t *surf)
{
    if (surf->drawflags & SURF_TRANS33)
        return MakeColor(255, 255, 255, 85);

    if (surf->drawflags & SURF_TRANS66)
        return MakeColor(255, 255, 255, 170);

    return U32_WHITE;
}

static glStateBits_t statebits_for_surface(const mface_t *surf)
{
    glStateBits_t statebits = GLS_DEFAULT;

    if (surf->drawflags & SURF_SKY) {
        if (surf->texinfo->image->flags & IF_CLASSIC_SKY)
            return GLS_CLASSIC_SKY;
        else
            return GLS_DEFAULT_SKY;
    }

    if (surf->drawflags & SURF_WARP)
        statebits |= GLS_WARP_ENABLE;

    if (surf->drawflags & SURF_TRANS_MASK)
        statebits |= GLS_COLOR_ENABLE | GLS_BLEND_BLEND | GLS_DEPTHMASK_FALSE;
    else if (surf->drawflags & SURF_ALPHATEST)
        statebits |= GLS_ALPHATEST_ENABLE;

    if (surf->drawflags & SURF_FLOWING) {
        statebits |= GLS_SCROLL_ENABLE;
        if (surf->drawflags & SURF_WARP)
            statebits |= GLS_SCROLL_SLOW;
    }

    if (surf->drawflags & SURF_N64_SCROLL_X)
        statebits |= GLS_SCROLL_ENABLE | GLS_SCROLL_X;

    if (surf->drawflags & SURF_N64_SCROLL_Y)
        statebits |= GLS_SCROLL_ENABLE | GLS_SCROLL_Y;

    if (surf->drawflags & SURF_N64_SCROLL_FLIP)
        statebits |= GLS_SCROLL_FLIP;

    return statebits;
}

static void build_surface_poly(mface_t *surf, vec_t *vbo, const uint32_t *normal_index)
{
    const bsp_t *bsp = gl_static.world.cache;
    const msurfedge_t *src_surfedge;
    const mvertex_t *src_vert;
    const medge_t *src_edge;
    const mtexinfo_t *texinfo = surf->texinfo;
    const uint32_t color = color_for_surface(surf);
    vec2_t scale, tc;
    box2_t box = Box2_Null();
    vec2i_t bmins, bmaxs;
    int i;

    // convert surface flags to state bits
    surf->statebits = statebits_for_surface(surf);

    // normalize texture coordinates
    scale.s = 1.0f / texinfo->image->width;
    scale.t = 1.0f / texinfo->image->height;

    if (surf->drawflags & SURF_N64_UV)
        scale = Vec2_Scale(scale, 0.5f);

    src_surfedge = surf->firstsurfedge;
    for (i = 0; i < surf->numsurfedges; i++) {
        src_edge = bsp->edges + src_surfedge->edge;
        src_vert = bsp->vertices + src_edge->v[src_surfedge->vert];
        src_surfedge++;

        // vertex coordinates
        Vec3_Store(vbo, src_vert->point);

        // vertex color
        WN32(vbo + 3, color);

        // texture coordinates
        tc.s = Vec3_Dot64(src_vert->point, texinfo->axis[0]) + texinfo->offset.s;
        tc.t = Vec3_Dot64(src_vert->point, texinfo->axis[1]) + texinfo->offset.t;

        vbo[4] = tc.s * scale.s;
        vbo[5] = tc.t * scale.t;

        // lightmap coordinates
        if (bsp->lm_decoupled) {
            vbo[6] = Vec3_Dot(src_vert->point, surf->lm_axis[0]) + surf->lm_offset.s;
            vbo[7] = Vec3_Dot(src_vert->point, surf->lm_axis[1]) + surf->lm_offset.t;
        } else {
            box = Box2_AddPoint(box, tc);
            vbo[6] = tc.s / 16;
            vbo[7] = tc.t / 16;
        }

        // normals
        if (normal_index && *normal_index < bsp->num_normals)
            Vec3_Store(vbo + 8, bsp->normals[*normal_index]);
        else if (surf->drawflags & DSURF_PLANEBACK)
            Vec3_Store(vbo + 8, Vec3_Negate(surf->plane->normal));
        else
            Vec3_Store(vbo + 8, surf->plane->normal);

        if (normal_index)
            normal_index++;

        // light styles
        memcpy(vbo + 11, surf->styles, sizeof(surf->styles));

        vbo += VERTEX_SIZE;
    }

    if (bsp->lm_decoupled)
        return;

    // calculate surface extents
    bmins.s = floorf(box.mins.s / 16);
    bmins.t = floorf(box.mins.t / 16);
    bmaxs.s = ceilf(box.maxs.s / 16);
    bmaxs.t = ceilf(box.maxs.t / 16);

    surf->lm_axis[0] = Vec3_Scale(texinfo->axis[0], 1.0f / 16);
    surf->lm_axis[1] = Vec3_Scale(texinfo->axis[1], 1.0f / 16);
    surf->lm_offset.s = texinfo->offset.s / 16 - bmins.s;
    surf->lm_offset.t = texinfo->offset.t / 16 - bmins.t;
    surf->lm_width  = bmaxs.s - bmins.s + 1;
    surf->lm_height = bmaxs.t - bmins.t + 1;

    for (i = 0; i < surf->numsurfedges; i++) {
        vbo -= VERTEX_SIZE;
        vbo[6] -= bmins.s;
        vbo[7] -= bmins.t;
    }
}

static void calc_surface_hash(mface_t *surf)
{
    uint32_t args[] = {
        surf->texinfo->image - r_images,
        surf->lm_texnum, surf->statebits
    };
    struct mdfour md;
    uint8_t out[16];

    mdfour_begin(&md);
    mdfour_update(&md, (uint8_t *)args, sizeof(args));
    mdfour_result(&md, out);

    surf->hash = 0;
    for (int i = 0; i < 16; i++)
        surf->hash ^= out[i];
}

static void upload_world_surfaces(void)
{
    const bsp_t *bsp = gl_static.world.cache;
    const uint32_t *normal_index = bsp->normal_indices;
    size_t size = 0;
    vec_t *vbo, *data;
    mface_t *surf;
    int i, currvert = 0;

    // calculate vertex buffer size in bytes
    for (i = 0, surf = bsp->faces; i < bsp->numfaces; i++, surf++)
        if (!(surf->drawflags & SURF_NODRAW))
            size += surf->numsurfedges * VERTEX_SIZE * sizeof(vbo[0]);

    // allocate temporary vertex buffer
    Com_DPrintf("%s: %zu bytes of vertex data\n", __func__, size);
    vbo = data = R_Malloc(size);

    // begin building lightmaps
    LM_BeginBuilding();

    for (i = 0, surf = bsp->faces; i < bsp->numfaces; i++, surf++) {
        if (surf->drawflags & SURF_NODRAW) {
            if (normal_index)
                normal_index += surf->numsurfedges;
            continue;
        }

        surf->firstvert = currvert;
        build_surface_poly(surf, vbo, normal_index);

        surf->lm_texnum = 0;    // start with no lightmap
        LM_BuildSurface(surf, vbo);
        vbo += surf->numsurfedges * VERTEX_SIZE;

        calc_surface_hash(surf);

        currvert += surf->numsurfedges;
        if (normal_index)
            normal_index += surf->numsurfedges;
    }

    // end building lightmaps
    LM_EndBuilding();

    // upload the vertex buffer data
    qglGenBuffers(1, &gl_static.world.buffer);
    GL_BindBuffer(GL_ARRAY_BUFFER, gl_static.world.buffer);
    GL_StaticBufferData(GL_ARRAY_BUFFER, size, data);
    Z_Free(data);
}

static void set_world_size(const mnode_t *node)
{
    vec3_t size = Box3_Size(node->box);
    float d = max(max(size.x, size.y), size.z);

    if (d > 4096)
        gl_static.world.size = 8192;
    else if (d > 2048)
        gl_static.world.size = 4096;
    else
        gl_static.world.size = 2048;
}

void GL_FreeWorld(void)
{
    if (!gl_static.world.cache)
        return;

    BSP_Free(gl_static.world.cache);
    GL_DeleteBuffers(1, &gl_static.world.buffer);

    if (gls.currentva == VA_3D)
        gls.currentva = VA_NONE;

    memset(&gl_static.world, 0, sizeof(gl_static.world));
}

static const mnode_t *find_face_node(const bsp_t *bsp, const mface_t *face)
{
    const mnode_t *node;
    int i, left, right;

    left = 0;
    right = bsp->numnodes - 1;
    while (left <= right) {
        i = (left + right) / 2;
        node = &bsp->nodes[i];
        if (node->firstface + node->numfaces <= face)
            left = i + 1;
        else if (node->firstface > face)
            right = i - 1;
        else
            return node;
    }

    return NULL;
}

static bool is_removable_sky(const mface_t *face)
{
    return face->drawflags & SURF_SKY &&
        face->texinfo->flags & (SURF_LIGHT | SURF_NODRAW);
}

static void remove_fake_sky_faces(const bsp_t *bsp)
{
    const mleaf_t *leaf;
    const mnode_t *node;
    int i, j, k, count = 0;
    mface_t *face;

    // find CONTENTS_MIST leafs
    for (i = 1, leaf = bsp->leafs + i; i < bsp->numleafs; i++, leaf++) {
        if (!(leaf->contents & CONTENTS_MIST))
            continue;

        // remove sky faces in this leaf
        for (j = 0; j < leaf->numleaffaces; j++) {
            face = leaf->firstleafface[j];
            if (!is_removable_sky(face))
                continue;

            face->drawflags = SURF_NODRAW;
            count++;

            // find node this face is on
            node = find_face_node(bsp, face);
            if (!node) {
                Com_DPrintf("Sky face node not found\n");
                continue;
            }

            // remove other sky faces on this node
            for (k = 0, face = node->firstface; k < node->numfaces; k++, face++) {
                if (is_removable_sky(face)) {
                    face->drawflags = SURF_NODRAW;
                    count++;
                }
            }
        }
    }

    if (count)
        Com_DPrintf("Removed %d fake sky faces\n", count);
}

void GL_LoadWorld(const char *name)
{
    char buffer[MAX_QPATH];
    bsp_t *bsp;
    mtexinfo_t *info;
    mface_t *surf;
    int i, n64surfs, ret;

    if (!name || !*name)
        return;

    Q_concat(buffer, sizeof(buffer), "maps/", name, ".bsp");
    ret = BSP_Load(buffer, &bsp);
    if (!bsp)
        Com_Error(ERR_DROP, "%s: couldn't load %s: %s",
                  __func__, buffer, BSP_ErrorString(ret));

    // check if the required world model was already loaded
    if (gl_static.world.cache == bsp) {
        for (i = 0; i < bsp->numtexinfo; i++)
            bsp->texinfo[i].image->registration_sequence = r_registration_sequence;

        for (i = 0; i < bsp->numnodes; i++)
            bsp->nodes[i].visframe = 0;

        for (i = 0; i < bsp->numleafs; i++)
            bsp->leafs[i].visframe = 0;

        Com_DPrintf("%s: reused old world model\n", __func__);
        bsp->refcount--;
        return;
    }

    // free previous model, if any
    GL_FreeWorld();

    // delete occlusion queries
    GL_DeleteQueries();
    GL_InitQueries();

    gl_static.world.cache = bsp;

    // calculate world size for far clip plane and sky box
    set_world_size(bsp->nodes);

    // register all texinfo
    for (i = 0, info = bsp->texinfo; i < bsp->numtexinfo; i++, info++) {
        if (info->flags & SURF_SKY) {
            if (Q_stristr(info->name, "env/sky")) {
                Q_concat(buffer, sizeof(buffer), "textures/", info->name, ".tga");
                info->image = IMG_Find(buffer, IT_SKY, IF_REPEAT | IF_CLASSIC_SKY);
            } else if (Q_stricmpn(info->name, CONST_STR_LEN("sky/")) == 0) {
                Q_concat(buffer, sizeof(buffer), info->name, ".tga");
                info->image = IMG_Find(buffer, IT_SKY, IF_CUBEMAP);
            } else {
                info->image = R_SKYTEXTURE;
            }
        } else if (info->flags & SURF_NODRAW && bsp->has_bspx) {
            info->image = R_NOTEXTURE;
        } else {
            imageflags_t flags = (info->flags & SURF_WARP) ? IF_TURBULENT : IF_NONE;
            Q_concat(buffer, sizeof(buffer), "textures/", info->name, ".wal");
            info->image = IMG_Find(buffer, IT_WALL, flags);
        }
    }

    // setup drawflags, etc
    for (i = n64surfs = 0, surf = bsp->faces; i < bsp->numfaces; i++, surf++) {
        // hack surface flags into drawflags for faster access
        surf->drawflags |= surf->texinfo->flags & ~DSURF_PLANEBACK;

        // clear statebits from previous load
        surf->statebits = GLS_DEFAULT;

        // ignore NODRAW bit on sky faces (but see below)
        if (surf->drawflags & SURF_SKY)
            surf->drawflags &= ~SURF_NODRAW;

        // ignore NODRAW bit in vanilla maps for compatibility
        if (surf->drawflags & SURF_NODRAW) {
            if (bsp->has_bspx)
                continue;
            surf->drawflags &= ~SURF_NODRAW;
        }

        if (surf->drawflags & (SURF_N64_UV | SURF_N64_SCROLL_X | SURF_N64_SCROLL_Y))
            n64surfs++;
    }

    // remove fake sky faces in vanilla maps
    if (!bsp->has_bspx)
        remove_fake_sky_faces(bsp);

    gl_static.nolm_mask = SURF_NOLM_MASK_DEFAULT;
    gl_static.nodraw_mask = SURF_SKY | SURF_NODRAW;

    // allow lightmapped liquids in BSPX and N64 maps
    if (bsp->has_bspx || n64surfs > 100)
        gl_static.nolm_mask = SURF_NOLM_MASK_REMASTER;

    // allow bmodel skies in BSPX maps
    if (bsp->has_bspx)
        gl_static.nodraw_mask = SURF_NODRAW;

    // post process all surfaces
    upload_world_surfaces();

    GL_ShowErrors(__func__);
}
