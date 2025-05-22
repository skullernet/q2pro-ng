// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#define STEPSIZE    18.0f

#define STOP_EPSILON 0.1f

typedef enum {
    GOOD_POSITION,
    STUCK_FIXED,
    NO_GOOD_POSITION
} stuck_result_t;

stuck_result_t G_FixStuckObject_Generic(vec3_t origin, const vec3_t own_mins, const vec3_t own_maxs,
                                        int ignore, contents_t mask, trace_func_t trace_func);

typedef struct {
    float airaccel;
    bool n64_physics;
} pm_config_t;

extern pm_config_t pm_config;

void PM_StepSlideMove_Generic(vec3_t origin, vec3_t velocity, float frametime, const vec3_t mins, const vec3_t maxs,
                              int passent, contents_t mask, touch_list_t *touch, bool has_time, trace_func_t trace_func);

void Pmove(pmove_t *pmove);

void G_AddBlend(float r, float g, float b, float a, vec4_t v_blend);
