/*
 * tutorial/tut_5_3.c
 *
 * A multi-threaded CPU + CUDA version of the AWACS simulation.
 *
 * Adds Cimba multithreaded trials to the simulation model and physics from
 * tut_5_2.c. Uses all available CPU cores to run trials in Posix worker threads.
 * The terrain models are generated and stored in GPU VRAM, ume per available
 * GPU in the system. The trials are assigned to GPUs to run the same parameter
 * combination on several different terrain maps (if available).
 *
 * Generates a Gnuplot chart of simulation results, not per-trial animation.
 *
 * Copyright (c) Asbjørn M. Bonvik 2026.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cimba.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

/* Shared C/CUDA data structures */
#include "tut_5_3.h"

/* Bit masks to distinguish between types of user-defined logging messages. */
#define USER_TRIALS 0x00000001
#define USER_SENSOR 0x00000002
#define USER_TARGET 0x00000004
#define USER_ALL    0x0FFFFFFF

/* Radar targets spread uniformly about the map */
#define NUM_TARGETS 1000

/* Sensor update interval, seconds. There are multiple sensor dwells inside
 * each update interval. The GPU handles these, see tut_5_3.h */
const float sensor_step_s = 1.0f;
/* Sensor beam width, degrees - a very simplified antenna model */
const float sensor_beam_width_d = 1.4f;

/* Geometric conversion constants */
const double arcsec_to_meters = 30.87;
const double nm_to_meters = 1852.0;
const double feet_to_meters = 0.3048;
const double knots_to_ms = (1852.0 / 3600.0);
const double deg_to_rad = (2.0 * M_PI / 360.0);
const double rad_to_deg = (360.0 / (2.0 * M_PI));

/* WGS84 constants semi-major axis and eccentricity */
const double WGS84_A = 6378137.0;
const double WGS84_F = (1.0 / 298.257223563);
const double WGS84_E2 = (WGS84_F * (2.0 - WGS84_F));

/* Constants to offset seed domains from each other */
#define SEED_TERRAIN 0xdecade01
#define SEED_TRIAL   0xdecade02

/*****************************************************************************
 * Our simulated world consists of these entities.
 */
struct simulation {
    struct platform *AWACS;
    struct target *targets;
};

/*
 * A single trial is defined by these parameters and generates these results.
 */
struct trial {
    /* Parameters */
    double flight_level;
    struct terrain *terrain;
    double dur_s;
    uint64_t seed;
    /* Results */
    int32_t num_found;
};

/*
 * The context for our simulation consists of the simulation entities, the
 * trial parameters, and the requested trial results.
 */
struct context {
    struct simulation *sim;
    struct trial *trl;
};

/*******************************************************************************
 * The terrain model, altitudes in meters referred to a Cartesian plane touching
 * Earth at the reference position.
 */

/* Allocate memory for the terrain object */
struct terrain *terrain_create(void)
{
    struct terrain *tp = cmi_malloc(sizeof(struct terrain));

    tp->cols = 0;
    tp->rows = 0;
    tp->map = NULL;

    return tp;
}

void terrain_destroy(struct terrain *tp)
{
    cmb_assert_release(tp != NULL);
    cmb_assert_release(tp->map == NULL);

    cmi_free(tp);
}

/****************************************************************************
 * Radar targets spread over the map surface
 */

struct target {
    /* Active process parent class */
    struct cmb_process core;
    /* Inherent parameters */
    float rcs_m2[4];
    float state_time_s[4];
    float height_m;
    struct terrain *terrain;
    /* Instantaneous state at last update */
    enum target_mode mode;
    enum target_detect_state tds;
    float rcs_now_m2;
    float time_s;
    float x_m;
    float y_m;
    float alt_m;
    float dir_r;
    float vel_ms;
    /* Cumulative state */
    bool detected;
};

/*
 * Target cmb_process function
 */
void *target_proc(struct cmb_process *me, void *vctx)
{
    cmb_assert_release(me != NULL);
    cmb_assert_release(vctx != NULL);

    struct target *tgt = (struct target *)me;
    const struct terrain *terp = (struct terrain *)vctx;

    /* Pick a location at random */
    tgt->time_s = (float)cmb_time();
    tgt->x_m = (float)cmb_random_uniform(terp->x_min, terp->x_max);
    tgt->y_m = (float)cmb_random_uniform(terp->y_min, terp->y_max);
    /* Actual terrain altitude will be added on the GPU */
    tgt->alt_m = tgt->height_m;

    /* Set initial status probabilistically */
    const double ph = tgt->state_time_s[HIDING] / (tgt->state_time_s[HIDING] + tgt->state_time_s[DRIVING]);
    tgt->mode = (cmb_random_bernoulli(ph)) ? HIDING : DRIVING;
    tgt->tds = UNDETERMINED;

    while (true) {
        if (tgt->mode == HIDING) {
            /* Stay hidden for a random time */
            tgt->rcs_now_m2 = tgt->rcs_m2[HIDING];
            tgt->time_s = (float)cmb_time();
            tgt->vel_ms = 0.0f;
            cmb_process_hold(cmb_random_exponential(tgt->state_time_s[HIDING]));

            /* Unmask */
            tgt->mode = STAGING;
            tgt->rcs_now_m2 = tgt->rcs_m2[STAGING];
            tgt->time_s = (float)cmb_time();
            /* Erlang distribution, mean equal to tgt->state_time_s[STAGING] */
            unsigned t_k = 10u;
            double t_m = tgt->state_time_s[STAGING] / (float)t_k;
            cmb_process_hold(cmb_random_erlang(t_k, t_m));

            /* Shoot & scoot */
            tgt->mode = FIRING;
            tgt->rcs_now_m2 = tgt->rcs_m2[FIRING];
            tgt->time_s = (float)cmb_time();
            /* Erlang distribution, mean equal to tgt->state_time_s[FIRING] */
            t_k = 20u;
            t_m = tgt->state_time_s[FIRING] / (float)t_k;
            cmb_process_hold(cmb_random_erlang(t_k, t_m));
            tgt->mode = DRIVING;
        }
        else {
            /* Choose a direction and speed at random, move for a random time */
            cmb_assert_debug(tgt->mode == DRIVING);
            tgt->rcs_now_m2 = tgt->rcs_m2[DRIVING];
            tgt->dir_r = (float)cmb_random_uniform(0.0, 2.0 * M_PI);
            tgt->vel_ms = (float)cmb_random_uniform(5.0, 20.0);
            /* Erlang distribution, mean equal to tgt->state_time_s[DRIVING] */
            const unsigned t_k = 5u;
            const double t_m = tgt->state_time_s[DRIVING] / (float)t_k;
            tgt->time_s = (float)cmb_time();
            cmb_process_hold(cmb_random_erlang(t_k, t_m));
            tgt->mode = HIDING;
        }
    }
}

struct target *target_create(void)
{
    struct target *tgt = cmi_malloc(sizeof(struct target));

    return tgt;
}

void target_initialize(struct target *tgt,
                       const char *tgt_name,
                       const float height,
                       const float rcs_hiding,
                       const float rcs_staging,
                       const float rcs_firing,
                       const float rcs_driving,
                       const float dur_hiding,
                       const float dur_staging,
                       const float dur_firing,
                       const float dur_driving,
                       struct terrain *terp)
{
    cmb_assert_release(tgt != NULL);

    tgt->height_m = height;
    tgt->rcs_m2[0] = rcs_hiding;
    tgt->rcs_m2[1] = rcs_staging;
    tgt->rcs_m2[2] = rcs_firing;
    tgt->rcs_m2[3] = rcs_driving;
    tgt->state_time_s[0] = dur_hiding * 3600.0f;
    tgt->state_time_s[1] = dur_staging * 60.0f;
    tgt->state_time_s[2] = dur_firing;  /* In seconds already */
    tgt->state_time_s[3] = dur_driving * 3600.0f;
    tgt->terrain = terp;
    tgt->tds = UNDETERMINED;
    tgt->time_s = 0.0f;
    tgt->x_m = 0.0f;
    tgt->y_m = 0.0f;
    tgt->alt_m = 0.0f;
    tgt->dir_r = 0.0f;
    tgt->vel_ms = 0.0f;
    tgt->detected = false;

    cmb_process_initialize((struct cmb_process *)tgt, tgt_name, target_proc, terp, 0);
}

void target_terminate(struct target *tgt)
{
    cmb_assert_release(tgt != NULL);

    cmb_process_terminate((struct cmb_process *)tgt);
}

void target_destroy(struct target *tgt)
{
    cmb_assert_release(tgt != NULL);

    cmi_free(tgt);
}

/****************************************************************************
 * Geometry of the AWACS racetrack orbit
 */
struct racetrack {
    /* Parameters */
    float start_time;      /* Simulation time when first at anchor point */
    float anchor_lat_r;    /* Startpoint for hot leg, radians latitude */
    float anchor_lon_r;    /* Startpoint for hot leg, radians longitude */
    float orientation_r;   /* Direction of hot leg, radians from north  */
    float length_m;        /* Length of each leg in meters */
    float turn_radius_m;   /* In meters */
    float altitude_m;      /* In meters */
    float velocity_ms;     /* In meters per second */
    bool clockwise;        /* true for standard orbit, false for non-standard */

    /* Pre-calculated from parameters*/
    float turn_dist_m;     /* Length of turn perimeter, meters */
    float roll_angle_r;    /* Platform banking angle during turns, radians */
    float orbit_dist_m;    /* Length of entire orbit, meters */
    float orbit_time_s;    /* Duration of a full orbit, seconds */
    float side_multiplier; /* 1.0 for clockwise, -1.0 for anti-clockwise */
    float lat_r2m;         /* Local conversion radians to meters, latitude */
    float lon_r2m;         /* Local conversion radians to meters, longitude */
    float local_earth_radius_m;         /* Earth radius at the anchor point */
};

struct racetrack *racetrack_create(void)
{
    struct racetrack *rt = cmi_malloc(sizeof(struct racetrack));

    return rt;
}

void racetrack_initialize(struct racetrack *rt,
                          const float start_time,      /* Simulation time, hours */
                          const float anchor_lat,      /* Degrees, start of hot leg */
                          const float anchor_lon,      /* Degrees, start of hot leg */
                          const float orientation,     /* Degrees clockwise, north = 0 */
                          const float leg_length,      /* Nautical miles */
                          const float turn_radius,     /* Nautical miles */
                          const float flight_level,    /* FL; feet / 100 */
                          const float velocity,        /* Knots */
                          const bool clockwise)
{
    cmb_assert_release(rt != NULL);

    rt->start_time = 3600.0f * start_time;
    rt->anchor_lat_r = (float)(anchor_lat * deg_to_rad);
    rt->anchor_lon_r = (float)(anchor_lon * deg_to_rad);
    rt->orientation_r = (float)((90.0 - orientation) * deg_to_rad);
    rt->length_m = (float)(leg_length * nm_to_meters);
    rt->turn_radius_m = (float)(turn_radius * nm_to_meters);
    rt->altitude_m = (float)(flight_level * 100.0 * feet_to_meters);
    rt->velocity_ms = (float)(velocity * knots_to_ms);
    rt->clockwise = clockwise;

    rt->turn_dist_m = M_PI * rt->turn_radius_m;
    rt->orbit_dist_m = 2.0f * (rt->length_m + rt->turn_dist_m);
    rt->orbit_time_s = rt->orbit_dist_m / rt->velocity_ms;
    rt->side_multiplier = (rt->clockwise) ? -1.0f : 1.0f;

    /* WGS84 calculation of meters per degree at anchor point */
    const double sin_lat = sinf(rt->anchor_lat_r);
    const double cos_lat = cosf(rt->anchor_lat_r);

    const double common = 1.0 - (WGS84_E2 * sin_lat * sin_lat);
    const double sqrt_common = sqrt(common);

    /* Radius of curvature in the meridian (for north-south) */
    const double M = WGS84_A * (1.0 - WGS84_E2) / (common * sqrt_common);

    /* Radius of curvature in the prime vertical (for east-west) */
    const double N = WGS84_A / sqrt_common;

    /* Local radius of the WGS84 Earth ellipsoid at the anchor point */
    rt->local_earth_radius_m = (float)sqrt(M * N);

    /* Add flight altitude for a correct conversion rad => m at that level */
    rt->lat_r2m = (float)(M + rt->altitude_m);
    rt->lon_r2m = (float)((N + rt->altitude_m) * cos_lat);

    /* Calculate the roll angle for a coordinated turn */
    const double g = 9.80665;
    const double roll_mag = atan((rt->velocity_ms * rt->velocity_ms) / (rt->turn_radius_m * g));

    rt->roll_angle_r = (float)(roll_mag * -rt->side_multiplier);
}

void racetrack_terminate(struct racetrack *rt)
{
    cmb_unused(rt);
}

void racetrack_destroy(struct racetrack *rt)
{
    cmb_assert_release(rt != NULL);

    cmi_free(rt);
}

void racetrack_write_vtp(const struct racetrack *rt, const char *filename);

/******************************************************************************
 *  The sensor and the platform carrying it. The sensor is the active component,
 *  while the platform just recalculates its state whenever it is asked to.
 *  Internal units SI, i.e., meters, radians, seconds, radians.
 */
struct platform_state {
    float x;
    float y;
    float dir;
    float rol;
    float vel;
    float alt;
};

/*
 * Geometry update function called by the active sensor.
 */
static void platform_state_update(struct platform_state *state,
                                  const struct racetrack *rt,
                                  const double t)
{
    const double delta_t = t - rt->start_time;
    double d = fmod(delta_t * rt->velocity_ms, rt->orbit_dist_m);
    if (d < 0) {
        d += rt->orbit_dist_m;
    }

    /* What segment, and where in that segment? */
    double x_local, y_local, heading_local, roll_local;
    if (d < rt->length_m) {
        x_local = d;
        y_local = 0.0;
        heading_local = 0.0;
        roll_local = 0.0;
    }
    else if (d < rt->length_m + rt->turn_dist_m) {
        const double phi = (d - rt->length_m) / rt->turn_radius_m - M_PI / 2.0;
        x_local = rt->length_m + rt->turn_radius_m * cos(phi);
        y_local = rt->side_multiplier * rt->turn_radius_m * (1.0 + sin(phi));
        heading_local = (phi + M_PI / 2.0) * rt->side_multiplier;
        roll_local = rt->roll_angle_r;
    }
    else if (d < 2.0 * rt->length_m + rt->turn_dist_m) {
        const double d_seg = d - (rt->length_m + rt->turn_dist_m);
        x_local = rt->length_m - d_seg;
        y_local = rt->side_multiplier * 2.0 * rt->turn_radius_m;
        heading_local = M_PI;
        roll_local = 0.0f;
    }
    else {
        const double phi = (d - (2.0 * rt->length_m + rt->turn_dist_m)) / rt->turn_radius_m + M_PI / 2.0;
        x_local = rt->turn_radius_m * cos(phi);
        y_local = rt->side_multiplier * rt->turn_radius_m * (1.0 + sin(phi));
        heading_local = M_PI + (phi - M_PI / 2.0) * rt->side_multiplier;
        roll_local = rt->roll_angle_r;
    }

    /* Rotate to orbit orientation */
    const double rad_o = rt->orientation_r;
    const double cos_o = cos(rad_o);
    const double sin_o = sin(rad_o);

    /* Rotate local (x,y) by the orbit orientation */
    const double x_final = x_local * cos_o - y_local * sin_o;
    const double y_final = x_local * sin_o + y_local * cos_o;

    /* Apply cached WGS84 scales at anchor point */
    state->x = (float)x_final;
    state->y = (float)y_final;
    state->dir = (float)fmod(heading_local + rt->orientation_r + 2.0 * M_PI, 2.0 * M_PI);
    state->rol = (float)roll_local;
    state->vel = (float)rt->velocity_ms;
    state->alt = (float)rt->altitude_m;
}

/******************************************************************************
 * The sensor-carrying platform
 */
struct platform {
    struct platform_state state;
    struct sensor *radar;
    struct racetrack *orbit;
};

struct platform *platform_create(void)
{
    struct platform *pfp = cmi_malloc(sizeof(struct platform));

    return pfp;
}

void platform_initialize(struct platform *pfp)
{
    cmb_assert_release(pfp != NULL);
    pfp->state.x = 0.0f;
    pfp->state.y = 0.0f;
    pfp->state.dir = 0.0f;
    pfp->state.rol = 0.0f;
    pfp->state.vel = 0.0f;
    pfp->state.alt = 0.0f;
    pfp->radar = NULL;
    pfp->orbit = NULL;
}

void platform_terminate(struct platform *pfp)
{
    cmb_unused(pfp);
}

void platform_destroy(struct platform *pfp)
{
    cmb_assert_release(pfp != NULL);
    cmi_free(pfp);
}

/*
 * Calculate the state of the platform at current simulation time
 */
static void platform_update(struct platform *pfp)
{
    const struct racetrack *rt = pfp->orbit;
    const double t = cmb_time();
    struct platform_state *state = &(pfp->state);
    platform_state_update(state, rt, t);
}

/******************************************************************************
 * The active sensor process
 */
struct sensor {
    struct cmb_process proc;    /* Inheritance, not a pointer */
    struct platform *host;
    float cur_dir_r;
    float rpm;
    float elev_angle_min_r;
    float elev_angle_max_r;
    float beamwidth_r;          /* Antenna 3 dB beamwidth in azimuth, radians */
    float ref_range_m;          /* Reference range (meters) where SNR = 0 dB */
    float ref_rcs_m2;           /* Reference target cross-section (m^2) */
    struct radar_params radar;

    struct sensor_gpu_state gpu;    /* GPU detection pipeline state */
    /* Host-side scratch buffers used for target state before kernel launch */
    float *h_x, *h_y, *h_alt, *h_dir, *h_vel, *h_time, *h_height, *h_rcs;
    int   *h_mode;
    int   *h_tds_result;
    uint8_t *h_detected_result;

    struct vtkhdf_handle *hdf;
};

/*
 * The sensor cmb_process function
 */
void *sensor_proc(struct cmb_process *me, void *vctx)
{
    struct target *targets = vctx;
    struct sensor *senp = (struct sensor *)me;
    struct platform *host = senp->host;
    const struct racetrack *orbit = host->orbit;

    /* Total antenna rotation per host call (one simulated second). The
     * kernel divides this into n_dwells_per_step internal dwell steps. */
    const float rot_per_step = (float)(senp->rpm * (1.0f / 60.0f)
                                       * (2.0f * M_PI));
    const float rot_inc_per_dwell = rot_per_step / (float)n_dwells_per_step;

    while (true) {
        cmb_process_hold(1.0);

        /* Save the previous coordinates before updating */
        const float prev_hdg = host->state.dir;
        const float base_dir = senp->cur_dir_r;
        platform_update(host);

        /* Platform yaw between calls also rotates the beam relative to
         * the world frame. Distribute it evenly across the dwells. */
        const float ddir = host->state.dir - prev_hdg;
        const float effective_rot_inc = rot_inc_per_dwell
                                      + ddir / (float)n_dwells_per_step;

        /* Advance the antenna position by one full step's worth of
         * rotation, keeping cur_dir in [0, 2pi). */
        senp->cur_dir_r = base_dir + rot_per_step + ddir;
        while (senp->cur_dir_r >= 2.0f * (float)M_PI) {
            senp->cur_dir_r -= 2.0f * (float)M_PI;
        }
        while (senp->cur_dir_r <  0.0f) {
            senp->cur_dir_r += 2.0f * (float)M_PI;
        }

        /* --- Gather: pack target state into SoA --- */
        for (int i = 0; i < NUM_TARGETS; i++) {
            senp->h_x[i]      = targets[i].x_m;
            senp->h_y[i]      = targets[i].y_m;
            senp->h_alt[i]    = targets[i].alt_m;
            senp->h_dir[i]    = targets[i].dir_r;
            senp->h_vel[i]    = targets[i].vel_ms;
            senp->h_time[i]   = targets[i].time_s;
            senp->h_height[i] = targets[i].height_m;
            senp->h_rcs[i]    = targets[i].rcs_now_m2;
            senp->h_mode[i]   = (int)targets[i].mode;
        }

        /* --- GPU detection pipeline: 25 dwells in one kernel batch --- */
        sensor_gpu_step(&senp->gpu,
                        senp->h_x, senp->h_y, senp->h_alt,
                        senp->h_dir, senp->h_vel, senp->h_time,
                        senp->h_height, senp->h_rcs, senp->h_mode,
                        host->state.x, host->state.y, host->state.alt,
                        host->state.dir, host->state.rol,
                        base_dir, effective_rot_inc,
                        senp->beamwidth_r,
                        (float)cmb_time(),
                        orbit->local_earth_radius_m,
                        senp->elev_angle_min_r, senp->elev_angle_max_r,
                        senp->ref_range_m, senp->ref_rcs_m2,
                        &senp->radar,
                        senp->h_tds_result, senp->h_detected_result);

        /* --- Scatter: same as before --- */
        for (int i = 0; i < NUM_TARGETS; i++) {
            targets[i].x_m   = senp->h_x[i];
            targets[i].y_m   = senp->h_y[i];
            targets[i].alt_m = senp->h_alt[i];
            if (senp->h_vel[i] > 0.0f) {
                targets[i].time_s = (float)cmb_time();
            }
            targets[i].tds = (enum target_detect_state)senp->h_tds_result[i];
            if (senp->h_detected_result[i]) {
                targets[i].detected = true;
            }
        }
    }
}

struct sensor *sensor_create(void)
{
    struct sensor *senp = cmi_malloc(sizeof(struct sensor));

    return senp;
}

void sensor_initialize(struct sensor *senp, const char *name, const float rpm,
                       const float min_elev, const float max_elev,
                       const float ref_rng, const float ref_rcs,
                       struct platform *host, void *vctx,
                       const struct terrain *terp, const uint64_t seed,
                       cudaStream_t cstream)
{
    cmb_assert_release(senp != NULL);
    cmb_assert_release(host != NULL);

    senp->host = host;
    senp->cur_dir_r = (float)(M_PI / 2.0);
    senp->rpm = rpm;
    senp->elev_angle_min_r = min_elev;
    senp->elev_angle_max_r = max_elev;
    senp->beamwidth_r = (float)(sensor_beam_width_d * deg_to_rad);
    senp->ref_range_m = ref_rng * (float)nm_to_meters;
    senp->ref_rcs_m2 = ref_rcs;

    /* AN/APY-2-like reference values. Tutorial uses these as defaults;
     * a real scenario would tune them per radar mode. */
    senp->radar.range_res_m        = 150.0f;
    senp->radar.n_pulses_per_dwell = 12;
    senp->radar.cfar_n_ref         = 8;
    senp->radar.cfar_n_guard       = 1;
    senp->radar.cfar_alpha         = 5.62f;     /* Pfa ≈ 1e-6, N = 16 */
    senp->radar.noise_floor_norm   = 1.0f;       /* normalized; units arbitrary */
    senp->radar.cell_grid_n_range  = 4;
    senp->radar.cell_grid_n_cross  = 4;
    senp->radar.mdv_ms          = 4.0f;
    senp->radar.mti_improvement = 1000.0f;

    const size_t sz = NUM_TARGETS * sizeof(float);
    CUDA_CHECK(cudaHostAlloc((void **)&senp->h_x, sz, cudaHostAllocDefault));
    CUDA_CHECK(cudaHostAlloc((void **)&senp->h_y, sz, cudaHostAllocDefault));
    CUDA_CHECK(cudaHostAlloc((void **)&senp->h_alt, sz, cudaHostAllocDefault));
    CUDA_CHECK(cudaHostAlloc((void **)&senp->h_dir, sz, cudaHostAllocDefault));
    CUDA_CHECK(cudaHostAlloc((void **)&senp->h_vel, sz, cudaHostAllocDefault));
    CUDA_CHECK(cudaHostAlloc((void **)&senp->h_time, sz, cudaHostAllocDefault));
    CUDA_CHECK(cudaHostAlloc((void **)&senp->h_height, sz, cudaHostAllocDefault));
    CUDA_CHECK(cudaHostAlloc((void **)&senp->h_rcs, sz, cudaHostAllocDefault));

    CUDA_CHECK(cudaHostAlloc((void **)&senp->h_mode,
                          NUM_TARGETS * sizeof(int), cudaHostAllocDefault));
    CUDA_CHECK(cudaHostAlloc((void **)&senp->h_tds_result,
                              NUM_TARGETS * sizeof(int), cudaHostAllocDefault));
    CUDA_CHECK(cudaHostAlloc((void **)&senp->h_detected_result,
                              NUM_TARGETS * sizeof(uint8_t), cudaHostAllocDefault));

    sensor_gpu_init(&senp->gpu, NUM_TARGETS, seed, terp, cstream);

    cmb_process_initialize((struct cmb_process *)senp, name, sensor_proc, vctx, 0);
}

void sensor_terminate(struct sensor *senp)
{
    CUDA_CHECK(cudaFreeHost(senp->h_x));
    CUDA_CHECK(cudaFreeHost(senp->h_y));
    CUDA_CHECK(cudaFreeHost(senp->h_alt));
    CUDA_CHECK(cudaFreeHost(senp->h_dir));
    CUDA_CHECK(cudaFreeHost(senp->h_vel));
    CUDA_CHECK(cudaFreeHost(senp->h_time));
    CUDA_CHECK(cudaFreeHost(senp->h_height));
    CUDA_CHECK(cudaFreeHost(senp->h_rcs));
    CUDA_CHECK(cudaFreeHost(senp->h_mode));
    CUDA_CHECK(cudaFreeHost(senp->h_tds_result));
    CUDA_CHECK(cudaFreeHost(senp->h_detected_result));

    cmb_process_terminate((struct cmb_process *)senp);
}

void sensor_destroy(struct sensor *senp)
{
    cmi_free(senp);
}

/*
 * Event to close down the simulation.
 */
void end_sim(void *subject, void *object)
{
    cmb_unused(object);

    const struct simulation *sim = subject;
    struct target *tgts = sim->targets;

    cmb_process_stop((struct cmb_process *)(sim->AWACS->radar), NULL);
    for (unsigned ui = 0; ui < NUM_TARGETS; ui++) {
        cmb_process_stop((struct cmb_process *)&tgts[ui], NULL);
    }
}

/* Internal time unit seconds, print in HHH:MM:SS.sss format */
#define FMTBUFLEN 20

const char *hhmmss_formatter(const double t)
{
    static CMB_THREAD_LOCAL char fmtbuf[FMTBUFLEN];

    double tmp = t;
    const unsigned h = (unsigned)(tmp / 3600.0);
    tmp -= (double)(h * 3600);
    const unsigned m = (unsigned)(tmp / 60.0);
    tmp -= (double)(m * 60.0);
    const double s = tmp;
    (void)snprintf(fmtbuf, FMTBUFLEN, "%02d:%02d:%04.1f", h, m, s);

    return fmtbuf;
}

/*
 * Lookup for which CUDA stream to use on what device */
struct gpu_thread_ctx {
    int           n_gpus;
    cudaStream_t *streams;   /* streams[d] created while device d was current */
};

/*
 * The simulation driver function to execute one trial
 */
void run_trial(void *vtrl)
{
    cmb_assert_release(vtrl != NULL);
    struct trial *trl = vtrl;
    struct terrain *terp = trl->terrain;

    const int dev = terp->device;
    CUDA_CHECK(cudaSetDevice(dev));
    const struct gpu_thread_ctx *ctx = cimba_thread_context();
    cudaStream_t stream = ctx->streams[dev];

    cmb_random_initialize(trl->seed);
    cmb_logger_timeformatter_set(hhmmss_formatter);
    cmb_logger_user(stdout, USER_TRIALS, "Start trial: Flight level: %3.0f GPU: %d Seed: 0x%016" PRIx64,
                    trl->flight_level, dev, trl->seed);

    /* Set up our trial housekeeping, assuming random seed already set */
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_event_queue_initialize(0.0);

    /* Create and start the targets */
    struct simulation sim = {};
    sim.targets = cmi_calloc(NUM_TARGETS, sizeof(struct target));
    for (unsigned ui = 0; ui < NUM_TARGETS; ui++) {
        char namebuf[16];
        const size_t np = snprintf(namebuf, sizeof(namebuf), "Target_%d", ui);
        cmb_assert_release(np < sizeof(namebuf));
        const float height = 2.0f;      /* Meters above ground */
        const float rcs_hide = 5.0f;    /* Square meters */
        const float rcs_stage = 100.0f; /* Square meters */
        const float rcs_fire = 1000.0f; /* Square meters */
        const float rcs_drive = 50.0f;  /* Square meters */
        const float time_hide = 3.0f;   /* Hours */
        const float time_stage = 5.0f;  /* Minutes */
        const float time_fire = 30.0f;  /* Seconds */
        const float time_drive = 1.0f;  /* Hours */
        target_initialize(&sim.targets[ui], namebuf, height,
                          rcs_hide, rcs_stage, rcs_fire, rcs_drive,
                          time_hide, time_stage, time_fire, time_drive,
                          trl->terrain);

        cmb_process_start((struct cmb_process *)&sim.targets[ui]);
    }

    /* Create and start the AWACS */
    struct racetrack *orbit = racetrack_create();
    const float start_time = 0.0f;      /* Hours */
    const float anchor_lat = 30.0f;     /* Degrees */
    const float anchor_lon = -10.0f;    /* Degrees */
    const float orientation = 0.0f;     /* Degrees, nautical */
    const float length = 50.0f;         /* Nautical miles */
    const float turn_radius = 10.0f;    /* Nautical miles */
    const float flight_level = trl->flight_level;
    const float velocity = 300.0f;      /* Knots */
    const bool clockwise = true;
    racetrack_initialize(orbit, start_time, anchor_lat, anchor_lon, orientation,
                         length, turn_radius, flight_level, velocity, clockwise);

    struct platform *awacs = platform_create();
    sim.AWACS = awacs;
    platform_initialize(awacs);
    awacs->orbit = orbit;
    /* Set initial position and direction coordinates */
    platform_update(awacs);

    struct sensor *radar = sensor_create();
    sim.AWACS->radar = radar;
    const float rpm = 6.0f;
    const float max_elev = (float)(60.0 * deg_to_rad);
    const float min_elev = (float)(-20.0 * deg_to_rad);
    const float ref_range = 150.0f;     /* Nautical miles */
    const float ref_rcs = 1.0f;
    sensor_initialize(radar, "AN/APY-2", rpm, min_elev, max_elev,
                      ref_range, ref_rcs, awacs, sim.targets,
                      terp, trl->seed, stream);
    cmb_process_start((struct cmb_process *)radar);

    /* Schedule the simulation control events */
    double t_end_s = trl->dur_s;
    cmb_event_schedule(end_sim, &sim, NULL, t_end_s, 0);

    /* Run this trial */
    fflush(stdout);
    cmb_event_queue_execute();

    trl->num_found = 0;
    for (unsigned ui = 0; ui < NUM_TARGETS; ui++) {
        if (sim.targets[ui].detected) {
            trl->num_found++;
        }
    }

    cmb_logger_user(stdout, USER_TRIALS, "End trial: Flight level: %3.0f Found: %d",
                    trl->flight_level, trl->num_found);

    racetrack_terminate(orbit);
    racetrack_destroy(orbit);
    sensor_terminate(radar);
    sensor_destroy(radar);
    platform_terminate(awacs);
    platform_destroy(awacs);
    for (unsigned ui = 0; ui < NUM_TARGETS; ui++) {
        target_terminate(&sim.targets[ui]);
    }

    cmi_free(sim.targets);

    /* Final housekeeping to leave everything as we found it */
    cmb_event_queue_terminate();
    cmb_random_terminate();
}

/* Bait for the thread hooks: Set up and close down CUDA streams per thread */
static void *gpu_thread_init(uint64_t tid, void *usrarg)
{
    cmb_unused(tid);

    const int n_gpus = (int)(intptr_t)usrarg;
    struct gpu_thread_ctx *ctx = cmi_malloc(sizeof(*ctx));
    ctx->n_gpus  = n_gpus;
    ctx->streams = cmi_malloc((size_t)n_gpus * sizeof(cudaStream_t));
    for (int d = 0; d < n_gpus; d++) {
        CUDA_CHECK(cudaSetDevice(d));
        CUDA_CHECK(cudaStreamCreate(&ctx->streams[d]));
    }

    return ctx;
}

static void gpu_thread_exit(void *vctx)
{
    struct gpu_thread_ctx *ctx = vctx;
    if (ctx == NULL) return;
    for (int d = 0; d < ctx->n_gpus; d++) {
        cudaSetDevice(d);
        cudaStreamDestroy(ctx->streams[d]);
    }

    cmi_free(ctx->streams);
    cmi_free(ctx);
}

void write_gnuplot_commands(void);

int main(int argc, char **argv)
{
    uint64_t master_seed = cmb_random_hwseed();
    double dur_h = 6.0;
    uint32_t n_replications = 10u;
    uint32_t n_threads = 0u;

    int opt;
    while ((opt = getopt(argc, argv, "d:n:r:s:")) != -1) {
        switch (opt) {
            case 'd': {
                errno = 0;
                char *end = NULL;
                dur_h = strtod(optarg, &end);
                if (end == optarg        /* no digits consumed   */
                    || *end != '\0'      /* trailing garbage     */
                    || errno == ERANGE   /* overflow / underflow */
                    || dur_h <= 0.0) {   /* domain: hours > 0    */
                    fprintf(stderr, "Invalid -d value '%s' (expected hours > 0)\n", optarg);
                    return EXIT_FAILURE;
                    }
                break;
            }

            case 'n': {
                errno = 0;
                char *end = NULL;
                n_replications = strtoul(optarg, &end, 0);   /* base 0 => accepts 0x.., 0.. */
                if (end == optarg || *end != '\0' || errno == ERANGE || n_replications == 0u) {
                    fprintf(stderr, "Invalid -n value '%s' (expected nonzero integer)\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }

            case 'r': {
                errno = 0;
                char *end = NULL;
                n_threads = strtoul(optarg, &end, 0);   /* base 0 => accepts 0x.., 0.. */
                if (end == optarg || *end != '\0' || errno == ERANGE) {
                    fprintf(stderr, "Invalid -r value '%s' (expected integer)\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }

            case 's': {
                errno = 0;
                char *end = NULL;
                master_seed = strtoull(optarg, &end, 0);   /* base 0 => accepts 0x.., 0.. */
                if (end == optarg || *end != '\0' || errno == ERANGE || master_seed == 0u) {
                    fprintf(stderr, "Invalid -s value '%s' (expected nonzero integer)\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            }

            default: {
                fprintf(stderr, "Usage: %s [-d <hours>] [-n replications] [-r worker_threads] [-s <seed>]\n", argv[0]);
                return EXIT_FAILURE;
            }
        }
    }

    /* Allocate and generate the terrain map to be used for several trials */
    const float fwidth_nm = 1000.0f;
    const float fheight_nm = 1000.0f;
    const float ref_lat = 30.0f;
    const float ref_lon = -10.0f;

    /* What compute horsepower do we have available? */
    int n_gpus = 0;
    CUDA_CHECK(cudaGetDeviceCount(&n_gpus));
    if (n_gpus < 1) {
        fprintf(stderr, "No CUDA devices found\n");
        return EXIT_FAILURE;
    }

    const uint32_t n_terrains = (uint32_t)n_gpus;   /* one terrain per GPU */
    struct terrain **terrains = cmi_calloc(n_terrains, sizeof(*terrains));

    for (uint32_t t = 0u; t < n_terrains; t++) {
        CUDA_CHECK(cudaSetDevice((int)t));
        terrains[t] = terrain_create();

        /* Deterministic, distinct seed per terrain */
        const uint64_t terr_seed = cmb_random_fmix64(master_seed, SEED_TERRAIN + t);
        terrain_initialize(terrains[t], fwidth_nm, fheight_nm, ref_lat, ref_lon, terr_seed);

        /* Set biome tables per GPU stream */
        sensor_gpu_load();
     }

    printf("Setting up experiment\n");
    const double fl_start = 100.0;
    const double fl_step = 25.0;
    const uint32_t n_levels = 15u;
    const uint32_t n_trials = n_levels * n_terrains * n_replications;

    struct trial *experiment = cmi_calloc(n_trials, sizeof(*experiment));

    uint32_t ui_trl = 0u;
    double flt_lvl = fl_start;
    for (uint32_t ul = 0u; ul < n_levels; ul++) {
        for (uint32_t ut = 0u; ut < n_terrains; ut++) {
            for (uint32_t ur = 0u; ur < n_replications; ur++) {
                experiment[ui_trl].flight_level = flt_lvl;
                experiment[ui_trl].terrain = terrains[ut];
                experiment[ui_trl].seed = cmb_random_fmix64(master_seed, SEED_TRIAL + ui_trl);
                experiment[ui_trl].dur_s = dur_h * 3600.0;

                /* Sentinel value to detect aborted trials */
                experiment[ui_trl].num_found = -1;

                ui_trl++;
            }
        }

        flt_lvl += fl_step;
    }

    const uint32_t nthr_act = cimba_threads_use(n_threads);
    printf("Using %d threads\n", nthr_act);

    printf("Baiting the hooks\n");
    cimba_thread_hooks_set(gpu_thread_init, (void *)(intptr_t)n_gpus, gpu_thread_exit);
    printf("Running the simulation ...\n");
    cimba_run(experiment, n_trials, sizeof(*experiment), run_trial);

    printf("Finished, calculating stats\n");
    ui_trl = 0u;
    FILE *datafp = fopen("tut_5_3.dat", "w");
    fprintf(datafp, "# Flight level\tDetected\tConf_interval\n");
    for (unsigned ul = 0u; ul < n_levels; ul++) {
        const double fl = experiment[ui_trl].flight_level;
        struct cmb_datasummary cds;
        cmb_datasummary_initialize(&cds);
        const uint32_t n_smpl = n_terrains * n_replications;
        for (unsigned ui_rep = 0u; ui_rep < n_smpl; ui_rep++) {
            cmb_datasummary_add(&cds, experiment[ui_trl].num_found);
            ui_trl++;
        }

        cmb_assert_debug(cmb_datasummary_count(&cds) == n_smpl);
        const double sample_avg = cmb_datasummary_mean(&cds);
        const double sample_sd = cmb_datasummary_stddev(&cds);
        const double t_crit = 2.228;
        fprintf(datafp, "%f\t%f\t%f\n", fl, sample_avg, t_crit * sample_sd);
        cmb_datasummary_terminate(&cds);
    }

    fclose(datafp);
    cmi_free(experiment);

    for (uint32_t t = 0u; t < n_terrains; t++) {
        CUDA_CHECK(cudaSetDevice((int)t));
        terrain_terminate(terrains[t]);
        terrain_destroy(terrains[t]);
    }

    cmi_free(terrains);

    write_gnuplot_commands();
    if (system("gnuplot -persistent tut_5_3.gp") != 0) {
        cmb_logger_warning(stderr, "gnuplot launch failed");
    }
    return 0;
}

void write_gnuplot_commands(void)
{
    FILE *cmdfp = fopen("tut_5_3.gp", "w");

    fprintf(cmdfp, "set terminal qt size 1200,700 enhanced font 'Arial,12'\n");
    fprintf(cmdfp, "set title \"Impact of AWACS flight level for detections\" font \"Times Bold, 18\" \n");
    fprintf(cmdfp, "set grid\n");
    fprintf(cmdfp, "set xlabel \"Flight level (ft x 100)\"\n");
    fprintf(cmdfp, "set ylabel \"Number of targets\"\n");
    fprintf(cmdfp, "set xrange [0.0:500.0]\n");
    fprintf(cmdfp, "set yrange [0:1000]\n");
    fprintf(cmdfp, "datafile = 'tut_5_3.dat'\n");
    fprintf(cmdfp, "plot datafile with yerrorbars lc rgb \"black\", \\\n");

    fclose(cmdfp);
}