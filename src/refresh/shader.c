/*
Copyright (C) 2018 Andrey Nazarov

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
#include "common/sizebuf.h"

#define MAX_SHADER_CHARS    8192

#define GLSL(x)     SZ_Write(buf, CONST_STR_LEN(#x "\n"));
#define GLSF(x)     SZ_Write(buf, CONST_STR_LEN(x))
#define GLSP(...)   shader_printf(buf, __VA_ARGS__)

q_printf(2, 3)
static void shader_printf(sizebuf_t *buf, const char *fmt, ...)
{
    va_list ap;
    size_t len;

    Q_assert(buf->cursize <= buf->maxsize);

    va_start(ap, fmt);
    len = Q_vsnprintf((char *)buf->data + buf->cursize, buf->maxsize - buf->cursize, fmt, ap);
    va_end(ap);

    Q_assert(len <= buf->maxsize - buf->cursize);
    buf->cursize += len;
}

static void write_header(sizebuf_t *buf, glStateBits_t bits)
{
#if USE_MD5
    if (bits & GLS_MESH_MD5 && gl_config.caps & QGL_CAP_SHADER_STORAGE) {
        if (gl_config.ver_es)
            GLSF("#version 310 es\n");
        else
            GLSF("#version 430\n");
    } else
#endif
    if (gl_config.ver_es) {
        GLSF("#version 300 es\n");
    } else if (gl_config.ver_sl >= QGL_VER(1, 40)) {
        GLSF("#version 140\n");
    } else {
        GLSF("#version 130\n");
        GLSF("#extension GL_ARB_uniform_buffer_object : require\n");
    }

    if (gl_config.ver_es) {
        GLSL(precision mediump float;)
        GLSL(precision mediump int;)
    }
}

static void write_block(sizebuf_t *buf, glStateBits_t bits)
{
    GLSF("layout(std140) uniform Uniforms {\n");
    GLSL(mat4 m_vp;);
    GLSL(mat4 m_model;);

    if (bits & GLS_MESH_ANY) {
        GLSL(
            vec3 u_old_scale;
            vec3 u_new_scale;
            vec3 u_translate;
            vec3 u_shadedir;
            vec4 u_color;
            vec4 pad_0;
            float pad_1;
            float pad_2;
            float pad_3;
            uint u_weight_ofs;
            uint u_jointnum_ofs;
            float u_shellscale;
            float u_backlerp;
            float u_frontlerp;
        )
    } else {
        GLSL(mat4 m_sky[2];)
    }

    GLSL(
        float u_time;
        float u_modulate_world;
        float u_modulate_entities;
        float u_fog_sky_factor;
        float u_heightfog_density;
        float u_heightfog_falloff;
        vec2 w_amp;
        vec2 w_phase;
        vec2 u_scroll;
        vec4 u_fog_color;
        vec4 u_heightfog_start;
        vec4 u_heightfog_end;
        vec4 u_vieworg;
    )
    GLSF("};\n");
}

static void write_lights_block(sizebuf_t *buf, glStateBits_t bits)
{
    GLSF("#define MAX_SHADOW_VIEWS " STRINGIFY(MAX_SHADOW_VIEWS) "\n");
    GLSF("#define MAX_DLIGHTS "      STRINGIFY(MAX_DLIGHTS)      "\n");
    GLSF("#define DLIGHT_CUTOFF "    STRINGIFY(DLIGHT_CUTOFF)    "\n");

    if (bits & GLS_SHADOWMAP_DRAW) {
        GLSL(
            struct ShadowView {
                mat4    matrix;
                vec4    offset;
            };

            layout(std140) uniform ShadowViews {
                ShadowView  u_shadow_views[MAX_SHADOW_VIEWS];
            };
        )

        if (gl_config.ver_es)
            GLSL(uniform highp)
        else
            GLSL(uniform)
        GLSL(sampler2DShadow u_shadowmap;)
    }

    GLSL(
        struct DynLight {
            vec3    origin;
            float   radius;
            vec3    color;
            int     firstview;
            vec3    dir;
            float   cone;
        };

        layout(std140) uniform Lights {
            int       u_num_spherelights;
            int       u_num_spotlights;
            DynLight  u_dlights[MAX_DLIGHTS];
        };
    )
}

static void write_dynamic_lights(sizebuf_t *buf, glStateBits_t bits)
{
    if (bits & GLS_SHADOWMAP_DRAW) {
        GLSL(
            int select_face(DynLight light) {
                vec3 ndir = normalize(v_world_pos - light.origin);
                vec3 adir = abs(ndir);
                vec3 sel1 = step(adir.yxx, adir.xyz) * step(adir.zzy, adir.xyz);
                vec3 sel2 = vec3(0.0, 1.0, 2.0) + step(vec3(0.0), ndir) * 3.0;
                return int(dot(sel1, sel2));
            }

            float calc_shadow(DynLight light, int face) {
                ShadowView view = u_shadow_views[light.firstview + face];
                vec4 frag = view.matrix * vec4(v_world_pos, 1.0);
                vec3 proj = frag.xyz / frag.w * 0.5 + 0.5;
                vec2 tc = proj.xy * view.offset.xy + view.offset.zw;
                return texture(u_shadowmap, vec3(tc, proj.z - 0.00001));
            }
        )
    } else {
        GLSF("#define calc_shadow(l, f) 1.0\n");
    }

    GLSL(vec3 calc_dynamic_lights() {
        vec3 result = vec3(0.0);

        for (int i = 0; i < u_num_spherelights; i++) {
            DynLight light = u_dlights[i];

            vec3 dir = light.origin - v_world_pos + v_world_norm * 16.0;
            float dist = length(dir);
            dir /= dist;

            float intens = max(light.radius - dist, 0.0) / (light.radius + DLIGHT_CUTOFF);
            intens *= max(dot(dir, v_world_norm), 1.0 - step(0.0, light.color.r));
            if (intens < 0.01)
                continue;

            if (light.firstview != -1)
                intens *= calc_shadow(light, select_face(light));

            result += light.color * intens;
        }

        for (int i = 0; i < u_num_spotlights; i++) {
            DynLight light = u_dlights[u_num_spherelights + i];

            vec3 dir = light.origin - v_world_pos;
            float dist = length(dir);
            dir /= dist;

            float intens = max(light.radius - dist, 0.0) / (light.radius + DLIGHT_CUTOFF);
            intens *= max(dot(dir, v_world_norm), 1.0 - step(0.0, light.color.r));

            intens *= 1.0 - (1.0 + dot(dir, light.dir)) / (1.0 - light.cone);
            if (intens < 0.01)
                continue;

            if (light.firstview != -1)
                intens *= calc_shadow(light, 0);

            result += light.color * intens;
        }

        return result;
    })
}

static void write_shadedot(sizebuf_t *buf)
{
    GLSL(float shadedot(vec3 normal) {
        float d = dot(normal, u_shadedir);
        if (d < 0.0)
            d *= 0.3;
        return d + 1.0;
    })
}

#if USE_MD5
static void write_skel_shader(sizebuf_t *buf, glStateBits_t bits)
{
    GLSL(
        struct Joint {
            vec4 pos;
            mat3x3 axis;
        };
        layout(std140) uniform Skeleton {
            Joint u_joints[256];
        };
    )

    if (gl_config.caps & QGL_CAP_SHADER_STORAGE) {
        GLSL(
            layout(std430, binding = 0) readonly buffer Weights {
                vec4 b_weights[];
            };

            layout(std430, binding = 1) readonly buffer JointNums {
                uint b_jointnums[];
            };
        )
    } else {
        GLSL(
            uniform samplerBuffer u_weights;
            uniform usamplerBuffer u_jointnums;
        )
    }

    GLSL(
        in vec2 a_tc;
        in vec3 a_norm;
        in uvec2 a_vert;

        out vec2 v_tc;
        out vec4 v_color;
    )

    if (bits & (GLS_FOG_HEIGHT | GLS_DYNAMIC_LIGHTS))
        GLSL(out vec3 v_world_pos;)

    if (bits & GLS_DYNAMIC_LIGHTS)
        GLSL(out vec3 v_world_norm;)

    if (bits & GLS_MESH_SHADE)
        write_shadedot(buf);

    GLSF("void main() {\n");
    GLSL(
        vec3 out_pos = vec3(0.0);
        vec3 out_norm = vec3(0.0);

        uint start = a_vert[0];
        uint count = a_vert[1];
    )

    GLSF("for (uint i = start; i < start + count; i++) {\n");
        if (gl_config.caps & QGL_CAP_SHADER_STORAGE) {
            GLSL(
                uint jointnum = b_jointnums[i / 4U];
                jointnum >>= (i & 3U) * 8U;
                jointnum &= 255U;

                vec4 weight = b_weights[i];
            )
        } else {
            GLSL(
                uint jointnum = texelFetch(u_jointnums, int(u_jointnum_ofs + i)).r;
                vec4 weight   = texelFetch(u_weights,   int(u_weight_ofs   + i));
            )
        }
        GLSL(
            Joint joint = u_joints[jointnum];

            vec3 wv = joint.pos.xyz + (weight.xyz * joint.axis) * joint.pos.w;
            out_pos += wv * weight.w;

            out_norm += a_norm * joint.axis * weight.w;
        )
    GLSF("}\n");

    GLSL(v_tc = a_tc;)

    if (bits & GLS_MESH_SHADE)
        GLSL(v_color = vec4(u_color.rgb * shadedot(out_norm), u_color.a);)
    else
        GLSL(v_color = u_color;)

    if (bits & GLS_MESH_SHELL)
        GLSL(out_pos += out_norm * u_shellscale;)

    if (bits & (GLS_FOG_HEIGHT | GLS_DYNAMIC_LIGHTS))
        GLSL(v_world_pos = (m_model * vec4(out_pos, 1.0)).xyz;)

    if (bits & GLS_DYNAMIC_LIGHTS)
        GLSL(v_world_norm = mat3(m_model) * out_norm;)

    GLSL(gl_Position = m_vp * vec4(out_pos, 1.0);)
    GLSF("}\n");
}
#endif

static void write_getnormal(sizebuf_t *buf)
{
    GLSL(vec3 get_normal(int norm) {
        const float pi = 3.14159265358979323846;
        const float scale = pi * (2.0 / 255.0);
        float lat = float( uint(norm)       & 255U) * scale;
        float lng = float((uint(norm) >> 8) & 255U) * scale;
        return vec3(
            sin(lat) * cos(lng),
            sin(lat) * sin(lng),
            cos(lat)
        );
    })
}

static void write_mesh_shader(sizebuf_t *buf, glStateBits_t bits)
{
    GLSL(
        in vec2 a_tc;
        in ivec4 a_new_pos;
    )

    if (bits & GLS_MESH_LERP)
        GLSL(in ivec4 a_old_pos;)

    GLSL(
        out vec2 v_tc;
        out vec4 v_color;
    )

    if (bits & (GLS_FOG_HEIGHT | GLS_DYNAMIC_LIGHTS))
        GLSL(out vec3 v_world_pos;)

    if (bits & GLS_DYNAMIC_LIGHTS)
        GLSL(out vec3 v_world_norm;)

    if (bits & GLS_MESH_NORMAL_MASK)
        write_getnormal(buf);

    if (bits & GLS_MESH_SHADE)
        write_shadedot(buf);

    GLSF("void main() {\n");
    GLSL(v_tc = a_tc;)

    if (bits & GLS_MESH_LERP) {
        if (bits & GLS_MESH_NORMAL_MASK)
            GLSL(
                vec3 old_norm = get_normal(a_old_pos.w);
                vec3 new_norm = get_normal(a_new_pos.w);
                vec3 norm = normalize(old_norm * u_backlerp + new_norm * u_frontlerp);
            )

        GLSL(vec3 pos = vec3(a_old_pos.xyz) * u_old_scale + vec3(a_new_pos.xyz) * u_new_scale + u_translate;)

        if (bits & GLS_MESH_SHELL)
            GLSL(pos += norm * u_shellscale;)

        if (bits & GLS_MESH_SHADE)
            GLSL(v_color = vec4(u_color.rgb * shadedot(norm), u_color.a);)
        else
            GLSL(v_color = u_color;)
    } else {
        if (bits & GLS_MESH_NORMAL_MASK)
            GLSL(vec3 norm = get_normal(a_new_pos.w);)

        GLSL(vec3 pos = vec3(a_new_pos.xyz) * u_new_scale + u_translate;)

        if (bits & GLS_MESH_SHELL)
            GLSL(pos += norm * u_shellscale;)

        if (bits & GLS_MESH_SHADE)
            GLSL(v_color = vec4(u_color.rgb * shadedot(norm), u_color.a);)
        else
            GLSL(v_color = u_color;)
    }

    if (bits & (GLS_FOG_HEIGHT | GLS_DYNAMIC_LIGHTS))
        GLSL(v_world_pos = (m_model * vec4(pos, 1.0)).xyz;)

    if (bits & GLS_DYNAMIC_LIGHTS)
        GLSL(v_world_norm = mat3(m_model) * norm;)

    GLSL(gl_Position = m_vp * vec4(pos, 1.0);)
    GLSF("}\n");
}

static void write_vertex_shader(sizebuf_t *buf, glStateBits_t bits)
{
    write_header(buf, bits);
    write_block(buf, bits);

#if USE_MD5
    if (bits & GLS_MESH_MD5) {
        write_skel_shader(buf, bits);
        return;
    }
#endif

    if (bits & GLS_MESH_MD2) {
        write_mesh_shader(buf, bits);
        return;
    }

    if (bits & GLS_LIGHTMAP_ENABLE) {
        GLSF("layout(std140) uniform Styles {\n");
        GLSF("float u_lightstyles[" STRINGIFY(MAX_LIGHTSTYLES) "];\n");
        GLSF("};\n");
    }

    GLSL(in vec4 a_pos;)
    if (bits & GLS_SKY_MASK) {
        GLSL(out vec3 v_dir;)
    } else {
        GLSL(in vec2 a_tc;)
        GLSL(out vec2 v_tc;)
    }

    if (bits & GLS_LIGHTMAP_ENABLE) {
        GLSL(
             in vec2 a_lmtc;
             out vec2 v_lmtc;
             in uvec4 a_lightstyle;
             flat out vec4 v_lightstyle;
        )
    }

    if (!(bits & GLS_TEXTURE_REPLACE)) {
        GLSL(in vec4 a_color;)
        GLSL(out vec4 v_color;)
    }

    if (bits & (GLS_FOG_HEIGHT | GLS_DYNAMIC_LIGHTS))
        GLSL(out vec3 v_world_pos;)

    if (bits & GLS_DYNAMIC_LIGHTS) {
        GLSL(in vec3 a_norm;)
        GLSL(out vec3 v_world_norm;)
    }

    GLSF("void main() {\n");
    if (bits & GLS_CLASSIC_SKY)
        GLSL(v_dir = (m_sky[1] * a_pos).xyz;)
    else if (bits & GLS_DEFAULT_SKY)
        GLSL(v_dir = (m_sky[0] * a_pos).xyz;)
    else if (bits & GLS_SCROLL_ENABLE)
        GLSL(v_tc = a_tc + u_scroll;)
    else
        GLSL(v_tc = a_tc;)

    if (bits & GLS_LIGHTMAP_ENABLE) {
        GLSL(
            v_lmtc = a_lmtc;
            v_lightstyle = vec4(
                u_lightstyles[a_lightstyle.x],
                u_lightstyles[a_lightstyle.y],
                u_lightstyles[a_lightstyle.z],
                u_lightstyles[a_lightstyle.w]
            );
        )
    }

    if (!(bits & GLS_TEXTURE_REPLACE))
        GLSL(v_color = a_color;)

    if (bits & (GLS_FOG_HEIGHT | GLS_DYNAMIC_LIGHTS))
        GLSL(v_world_pos = (m_model * a_pos).xyz;)

    if (bits & GLS_DYNAMIC_LIGHTS)
        GLSL(v_world_norm = mat3(m_model) * a_norm;)

    GLSL(gl_Position = m_vp * a_pos;)
    GLSF("}\n");
}

#define MAX_SIGMA   25
#define MAX_RADIUS  50

// https://lisyarus.github.io/blog/posts/blur-coefficients-generator.html
static void write_gaussian_blur(sizebuf_t *buf)
{
    float sigma = gl_static.bloom_sigma;
    int radius = min(sigma * 2 + 0.5f, MAX_RADIUS);
    int samples = radius + 1;
    int raw_samples = (radius * 2) + 1;
    float offsets[MAX_RADIUS + 1];
    float weights[(MAX_RADIUS * 2) + 1];

    // should not really happen
    if (radius < 1) {
        GLSL(vec4 blur(sampler2D src, vec2 tc, vec2 dir) { return texture(src, tc); })
        return;
    }

    float sum = 0;
    for (int i = -radius, j = 0; i <= radius; i++, j++) {
        float w = expf(-(i * i) / (sigma * sigma));
        weights[j] = w;
        sum += w;
    }

    for (int i = 0; i < raw_samples; i++)
        weights[i] /= sum;

    for (int i = -radius, j = 0; i <= radius; i += 2, j++) {
        if (i == radius) {
            offsets[j] = i;
            weights[j] = weights[i + radius];
        } else {
            float w0 = weights[i + radius + 0];
            float w1 = weights[i + radius + 1];
            float w = w0 + w1;

            if (w > 0)
                offsets[j] = i + w1 / w;
            else
                offsets[j] = i;

            weights[j] = w;
        }
    }

    GLSP("#define BLUR_SAMPLES %d\n", samples);

    GLSF("const float blur_offsets[BLUR_SAMPLES] = float[BLUR_SAMPLES](\n");
    for (int i = 0; i < samples - 1; i++)
        GLSP("%f, ", offsets[i]);
    GLSP("%f);\n", offsets[samples - 1]);

    GLSF("const float blur_weights[BLUR_SAMPLES] = float[BLUR_SAMPLES](\n");
    for (int i = 0; i < samples - 1; i++)
        GLSP("%f, ", weights[i]);
    GLSP("%f);\n", weights[samples - 1]);

    GLSL(vec4 blur(sampler2D src, vec2 tc, vec2 dir) {
        vec4 result = vec4(0.0);
        for (int i = 0; i < BLUR_SAMPLES; i++)
            result += texture(src, tc + dir * blur_offsets[i]) * blur_weights[i];
        return result;
    })
}

static void write_box_blur(sizebuf_t *buf)
{
    GLSL(vec4 blur(sampler2D src, vec2 tc, vec2 dir) {
        const float o = 0.25;
        vec4 result;
        result  = texture(src, tc + vec2(-o, -o) * dir) * o;
        result += texture(src, tc + vec2(-o,  o) * dir) * o;
        result += texture(src, tc + vec2( o, -o) * dir) * o;
        result += texture(src, tc + vec2( o,  o) * dir) * o;
        return result;
    })
}

// XXX: this is very broken. but that's how it is in re-release.
static void write_height_fog(sizebuf_t *buf, glStateBits_t bits)
{
    GLSL({
        float dir_z = normalize(v_world_pos - u_vieworg.xyz).z;
        float s = sign(dir_z);
        dir_z += 0.00001 * (1.0 - s * s);
        float eye = u_vieworg.z - u_heightfog_start.w;
        float pos = v_world_pos.z - u_heightfog_start.w;
        float density = (exp(-u_heightfog_falloff * eye) -
                         exp(-u_heightfog_falloff * pos)) / (u_heightfog_falloff * dir_z);
        float extinction = 1.0 - clamp(exp(-density), 0.0, 1.0);
        float fraction = clamp((pos - u_heightfog_start.w) / (u_heightfog_end.w - u_heightfog_start.w), 0.0, 1.0);
        vec3 fog_color = mix(u_heightfog_start.rgb, u_heightfog_end.rgb, fraction) * extinction;
        float fog = (1.0 - exp(-(u_heightfog_density * frag_depth))) * extinction;
        diffuse.rgb = mix(diffuse.rgb, fog_color.rgb, fog);
    )

    if (bits & GLS_BLOOM_GENERATE)
        GLSL(bloom.rgb *= 1.0 - fog;)

    GLSL(})
}

static void write_fragment_shader(sizebuf_t *buf, glStateBits_t bits)
{
    write_header(buf, bits);

    if (bits & GLS_SHADOWMAP_GENERATE) {
        GLSL(void main() {})
        return;
    }

    if (bits & GLS_UNIFORM_MASK)
        write_block(buf, bits);

    if (bits & GLS_DYNAMIC_LIGHTS)
        write_lights_block(buf, bits);

    if (bits & GLS_CLASSIC_SKY) {
        GLSL(
            uniform sampler2D u_texture1;
            uniform sampler2D u_texture2;
        )
    } else if (bits & GLS_DEFAULT_SKY) {
        GLSL(uniform samplerCube u_texture;)
    } else {
        GLSL(uniform sampler2D u_texture;)
        if (bits & GLS_BLOOM_OUTPUT)
            GLSL(uniform sampler2D u_bloom;)
    }

    if (bits & GLS_SKY_MASK)
        GLSL(in vec3 v_dir;)
    else
        GLSL(in vec2 v_tc;)

    if (bits & GLS_LIGHTMAP_ENABLE) {
        if (gl_config.ver_es)
            GLSL(uniform mediump)
        else
            GLSL(uniform)
        GLSL(
             sampler2DArray u_lightmap;
             in vec2 v_lmtc;
             flat in vec4 v_lightstyle;
        )
    }

    if (bits & GLS_GLOWMAP_ENABLE)
        GLSL(uniform sampler2D u_glowmap;)

    if (!(bits & GLS_TEXTURE_REPLACE))
        GLSL(in vec4 v_color;)

    if (gl_config.ver_es)
        GLSL(layout(location = 0))
    GLSL(out vec4 o_color;)

    if (bits & GLS_BLOOM_GENERATE) {
        if (gl_config.ver_es)
            GLSL(layout(location = 1))
        GLSL(out vec4 o_bloom;)
    }

    if (bits & (GLS_FOG_HEIGHT | GLS_DYNAMIC_LIGHTS))
        GLSL(in vec3 v_world_pos;)

    if (bits & GLS_DYNAMIC_LIGHTS)
        GLSL(in vec3 v_world_norm;)

    if (bits & GLS_BLUR_GAUSS)
        write_gaussian_blur(buf);
    else if (bits & GLS_BLUR_BOX)
        write_box_blur(buf);

    if (bits & GLS_DYNAMIC_LIGHTS)
        write_dynamic_lights(buf, bits);

    GLSF("void main() {\n");
    if (bits & GLS_CLASSIC_SKY) {
        GLSL(
            float len = length(v_dir);
            vec2 dir = v_dir.xy * (3.0 / len);
            vec2 tc1 = dir + vec2(u_time * 0.0625);
            vec2 tc2 = dir + vec2(u_time * 0.1250);
            vec4 solid = texture(u_texture1, tc1);
            vec4 alpha = texture(u_texture2, tc2);
            vec4 diffuse = vec4((solid.rgb - alpha.rgb * 0.25) * 0.65, 1.0);
        )
    } else if (bits & GLS_DEFAULT_SKY) {
        GLSL(vec4 diffuse = texture(u_texture, v_dir);)
    } else {
        GLSL(vec2 tc = v_tc;)

        if (bits & GLS_WARP_ENABLE)
            GLSL(tc += w_amp * sin(tc.ts * w_phase + u_time);)

        if (bits & GLS_BLUR_MASK)
            GLSL(vec4 diffuse = blur(u_texture, tc, u_fog_color.xy);)
        else
            GLSL(vec4 diffuse = texture(u_texture, tc);)
    }

    if (bits & GLS_ALPHATEST_ENABLE)
        GLSL(if (diffuse.a <= 0.666) discard;)

    if (bits & GLS_BLOOM_GENERATE)
        GLSL(vec4 bloom = vec4(0.0);)

    if (bits & GLS_LIGHTMAP_ENABLE) {
        GLSL(
            vec3 lightmap;
            lightmap  = texture(u_lightmap, vec3(v_lmtc, 0.0)).rgb * v_lightstyle[0];
            lightmap += texture(u_lightmap, vec3(v_lmtc, 1.0)).rgb * v_lightstyle[1];
            lightmap += texture(u_lightmap, vec3(v_lmtc, 2.0)).rgb * v_lightstyle[2];
            lightmap += texture(u_lightmap, vec3(v_lmtc, 3.0)).rgb * v_lightstyle[3];
        )

        if (bits & GLS_GLOWMAP_ENABLE) {
            GLSL(vec4 glowmap = texture(u_glowmap, tc);)
            GLSL(lightmap = mix(lightmap, vec3(1.0), glowmap.a);)

            if (bits & GLS_BLOOM_GENERATE)
                GLSL(bloom.rgb = diffuse.rgb * glowmap.a;)
        }

        if (bits & GLS_DYNAMIC_LIGHTS)
            GLSL(lightmap += calc_dynamic_lights();)

        GLSL(diffuse.rgb *= clamp(lightmap, 0.0, 1.0) * u_modulate_world;)
    }

    if (bits & GLS_DEFAULT_FLARE)
        GLSL(
             diffuse.rgb *= (diffuse.r + diffuse.g + diffuse.b) / 3.0;
             diffuse.rgb *= v_color.a;
        )

    if (!(bits & GLS_TEXTURE_REPLACE)) {
        GLSL(vec4 color = v_color;)
        if ((bits & (GLS_LIGHTMAP_ENABLE | GLS_DYNAMIC_LIGHTS)) == GLS_DYNAMIC_LIGHTS)
            GLSL(color.rgb += clamp(calc_dynamic_lights(), -1.0, 1.0) * u_modulate_entities;)
        GLSL(diffuse *= color;)
    }

    if ((bits & (GLS_LIGHTMAP_ENABLE | GLS_GLOWMAP_ENABLE)) == GLS_GLOWMAP_ENABLE) {
        GLSL(vec4 glowmap = texture(u_glowmap, tc);)
        GLSL(diffuse.rgb += glowmap.rgb;)

        if (bits & GLS_BLOOM_GENERATE)
            GLSL(bloom.rgb = glowmap.rgb;)
    }

    if (bits & GLS_BLOOM_GENERATE) {
        if (bits & GLS_BLOOM_SHELL)
            GLSL(bloom = diffuse;)
        else
            GLSL(bloom.a = diffuse.a;)
    }

    if (bits & (GLS_FOG_GLOBAL | GLS_FOG_HEIGHT))
        GLSL(float frag_depth = gl_FragCoord.z / gl_FragCoord.w;)

    if (bits & GLS_FOG_GLOBAL) {
        GLSL({
            float d = u_fog_color.a * frag_depth;
            float fog = 1.0 - exp(-(d * d));
            diffuse.rgb = mix(diffuse.rgb, u_fog_color.rgb, fog);
        )

        if (bits & GLS_BLOOM_GENERATE)
            GLSL(bloom.rgb *= 1.0 - fog;)

        GLSL(})
    }

    if (bits & GLS_FOG_HEIGHT)
        write_height_fog(buf, bits);

    if (bits & GLS_FOG_SKY)
        GLSL(diffuse.rgb = mix(diffuse.rgb, u_fog_color.rgb, u_fog_sky_factor);)

    if (bits & GLS_BLOOM_OUTPUT)
        GLSL(diffuse.rgb += texture(u_bloom, tc).rgb;)

    if (bits & GLS_BLOOM_GENERATE)
        GLSL(o_bloom = bloom;)

    GLSL(o_color = diffuse;)
    GLSF("}\n");
}

static GLuint create_shader(GLenum type, const sizebuf_t *buf)
{
    const GLchar *data = (const GLchar *)buf->data;
    GLint size = buf->cursize;

    GLuint shader = qglCreateShader(type);
    if (!shader) {
        Com_EPrintf("Couldn't create shader\n");
        return 0;
    }

    Com_DDDPrintf("Compiling %s shader (%d bytes):\n%.*s\n",
                  type == GL_VERTEX_SHADER ? "vertex" : "fragment", size, size, data);

    qglShaderSource(shader, 1, &data, &size);
    qglCompileShader(shader);
    GLint status = 0;
    qglGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char buffer[MAX_STRING_CHARS];

        buffer[0] = 0;
        qglGetShaderInfoLog(shader, sizeof(buffer), NULL, buffer);
        qglDeleteShader(shader);

        if (buffer[0])
            Com_Printf("%s", buffer);

        Com_EPrintf("Error compiling %s shader\n",
                    type == GL_VERTEX_SHADER ? "vertex" : "fragment");
        return 0;
    }

    return shader;
}

static bool bind_uniform_block(GLuint program, const char *name, size_t cpu_size, GLuint binding)
{
    GLuint index = qglGetUniformBlockIndex(program, name);
    if (index == GL_INVALID_INDEX) {
        Com_EPrintf("%s block not found\n", name);
        return false;
    }

    GLint gpu_size = 0;
    qglGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_DATA_SIZE, &gpu_size);
    if (gpu_size != cpu_size) {
        Com_EPrintf("%s block size mismatch: %d != %zu\n", name, gpu_size, cpu_size);
        return false;
    }

    qglUniformBlockBinding(program, index, binding);
    return true;
}

static void bind_texture_unit(GLuint program, const char *name, GLuint tmu)
{
    GLint loc = qglGetUniformLocation(program, name);
    if (loc == -1) {
        Com_EPrintf("Texture %s not found\n", name);
        return;
    }
    qglUniform1i(loc, tmu);
}

static GLuint create_and_use_program(glStateBits_t bits)
{
    char buffer[MAX_SHADER_CHARS];
    sizebuf_t sb;

    GLuint program = qglCreateProgram();
    if (!program) {
        Com_EPrintf("Couldn't create program\n");
        return 0;
    }

    SZ_Init(&sb, buffer, sizeof(buffer), "GLSL");
    write_vertex_shader(&sb, bits);
    GLuint shader_v = create_shader(GL_VERTEX_SHADER, &sb);
    if (!shader_v)
        goto fail;

    SZ_Clear(&sb);
    write_fragment_shader(&sb, bits);
    GLuint shader_f = create_shader(GL_FRAGMENT_SHADER, &sb);
    if (!shader_f) {
        qglDeleteShader(shader_v);
        goto fail;
    }

    qglAttachShader(program, shader_v);
    qglAttachShader(program, shader_f);

#if USE_MD5
    if (bits & GLS_MESH_MD5) {
        qglBindAttribLocation(program, VERT_ATTR_MESH_TC, "a_tc");
        qglBindAttribLocation(program, VERT_ATTR_MESH_NORM, "a_norm");
        qglBindAttribLocation(program, VERT_ATTR_MESH_VERT, "a_vert");
    } else
#endif
    if (bits & GLS_MESH_MD2) {
        qglBindAttribLocation(program, VERT_ATTR_MESH_TC, "a_tc");
        if (bits & GLS_MESH_LERP)
            qglBindAttribLocation(program, VERT_ATTR_MESH_OLD_POS, "a_old_pos");
        qglBindAttribLocation(program, VERT_ATTR_MESH_NEW_POS, "a_new_pos");
    } else {
        qglBindAttribLocation(program, VERT_ATTR_POS, "a_pos");
        if (!(bits & GLS_SKY_MASK))
            qglBindAttribLocation(program, VERT_ATTR_TC, "a_tc");
        if (bits & GLS_LIGHTMAP_ENABLE) {
            qglBindAttribLocation(program, VERT_ATTR_LMTC, "a_lmtc");
            qglBindAttribLocation(program, VERT_ATTR_NORMAL, "a_norm");
            qglBindAttribLocation(program, VERT_ATTR_LIGHTSTYLE, "a_lightstyle");
        }
        if (!(bits & GLS_TEXTURE_REPLACE))
            qglBindAttribLocation(program, VERT_ATTR_COLOR, "a_color");
    }

    if (bits & GLS_BLOOM_GENERATE && !gl_config.ver_es) {
        qglBindFragDataLocation(program, 0, "o_color");
        qglBindFragDataLocation(program, 1, "o_bloom");
    }

    qglLinkProgram(program);

    qglDeleteShader(shader_v);
    qglDeleteShader(shader_f);

    GLint status = 0;
    qglGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        char buffer[MAX_STRING_CHARS];

        buffer[0] = 0;
        qglGetProgramInfoLog(program, sizeof(buffer), NULL, buffer);

        if (buffer[0])
            Com_Printf("%s", buffer);

        Com_EPrintf("Error linking program\n");
        goto fail;
    }

    if (!bind_uniform_block(program, "Uniforms", sizeof(gls.u_block), UBO_UNIFORMS))
        goto fail;

    if (bits & GLS_DYNAMIC_LIGHTS)
        if (!bind_uniform_block(program, "Lights", sizeof(gls.u_lights), UBO_LIGHTS))
            goto fail;

    if (bits & GLS_SHADOWMAP_DRAW)
        if (!bind_uniform_block(program, "ShadowViews", sizeof(glr.shadow_views), UBO_SHADOWVIEWS))
            goto fail;

    if (bits & GLS_LIGHTMAP_ENABLE)
        if (!bind_uniform_block(program, "Styles", sizeof(gls.u_styles), UBO_STYLES))
            goto fail;

#if USE_MD5
    if (bits & GLS_MESH_MD5)
        if (!bind_uniform_block(program, "Skeleton", sizeof(glJoint_t) * MD5_MAX_JOINTS, UBO_SKELETON))
            goto fail;
#endif

    qglUseProgram(program);

    if (bits & GLS_CLASSIC_SKY) {
        bind_texture_unit(program, "u_texture1", TMU_TEXTURE);
        bind_texture_unit(program, "u_texture2", TMU_LIGHTMAP);
    } else if (!(bits & GLS_SHADOWMAP_GENERATE))
        bind_texture_unit(program, "u_texture", TMU_TEXTURE);

    if (bits & GLS_BLOOM_OUTPUT)
        bind_texture_unit(program, "u_bloom", TMU_LIGHTMAP);

    if (bits & GLS_LIGHTMAP_ENABLE)
        bind_texture_unit(program, "u_lightmap", TMU_LIGHTMAP);

    if (bits & GLS_GLOWMAP_ENABLE)
        bind_texture_unit(program, "u_glowmap", TMU_GLOWMAP);

    if (bits & GLS_SHADOWMAP_DRAW)
        bind_texture_unit(program, "u_shadowmap", TMU_SHADOWMAP);

#if USE_MD5
    if (bits & GLS_MESH_MD5 && !(gl_config.caps & QGL_CAP_SHADER_STORAGE)) {
        bind_texture_unit(program, "u_weights", TMU_SKEL_WEIGHTS);
        bind_texture_unit(program, "u_jointnums", TMU_SKEL_JOINTNUMS);
    }
#endif

    return program;

fail:
    qglDeleteProgram(program);
    return 0;
}

void GL_ShaderStateBits(glStateBits_t key)
{
    GLuint *prog = HashMap_Lookup(GLuint, gl_static.programs, &key);

    if (prog) {
        qglUseProgram(*prog);
    } else {
        GLuint val = create_and_use_program(key);
        HashMap_Insert(gl_static.programs, &key, &val);
    }
}

void GL_ShaderUpdateBlur(void)
{
    float sigma = Cvar_ClampValue(gl_bloom_sigma, 1, MAX_SIGMA) * glr.fd.height / 1080;
    if (gl_static.bloom_sigma == sigma)
        return;

    Com_DDPrintf("%s: %.1f\n", __func__, sigma);
    gl_static.bloom_sigma = sigma;

    bool changed = false;
    uint32_t map_size = HashMap_Size(gl_static.programs);
    for (int i = 0; i < map_size; i++) {
        glStateBits_t *bits = HashMap_GetKey(glStateBits_t, gl_static.programs, i);
        if (*bits & GLS_BLUR_GAUSS) {
            GLuint *prog = HashMap_GetValue(GLuint, gl_static.programs, i);
            qglDeleteProgram(*prog);
            *prog = create_and_use_program(*bits);
            changed = true;
        }
    }

    if (changed)
        GL_ShaderStateBits(gls.state_bits & GLS_SHADER_MASK);
}

void GL_InitShaders(void)
{
    gl_static.programs = HashMap_TagCreate(glStateBits_t, GLuint, HashInt64, NULL, TAG_RENDERER);

    qglGenBuffers(UBO_COUNT, gl_static.uniform_buffers);
    for (int i = UBO_UNIFORMS; i <= UBO_STYLES; i++)
        GL_BindBufferBase(GL_UNIFORM_BUFFER, i, gl_static.uniform_buffers[i]);

#if USE_MD5
    if (gl_config.caps & QGL_CAP_SKELETON_MASK) {
        GL_BindBufferBase(GL_UNIFORM_BUFFER, UBO_SKELETON, gl_static.uniform_buffers[UBO_SKELETON]);

        if ((gl_config.caps & QGL_CAP_SKELETON_MASK) == QGL_CAP_BUFFER_TEXTURE)
            qglGenTextures(SSBO_COUNT, gl_static.skeleton_tex);
    }
#endif
}

void GL_ShutdownShaders(void)
{
    qglUseProgram(0);

    if (gl_static.programs) {
        uint32_t map_size = HashMap_Size(gl_static.programs);
        for (int i = 0; i < map_size; i++) {
            GLuint *prog = HashMap_GetValue(GLuint, gl_static.programs, i);
            qglDeleteProgram(*prog);
        }
        HashMap_Destroy(gl_static.programs);
        gl_static.programs = NULL;
    }

    qglDeleteBuffers(UBO_COUNT, gl_static.uniform_buffers);

#if USE_MD5
    if ((gl_config.caps & QGL_CAP_SKELETON_MASK) == QGL_CAP_BUFFER_TEXTURE)
        qglDeleteTextures(SSBO_COUNT, gl_static.skeleton_tex);
#endif
}
