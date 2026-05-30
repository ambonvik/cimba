/*
 * tutorial/tut_5_1.c
 *
 * A single-threaded CPU only version of the AWACS simulation as a baseline for
 * adding CUDA GPGPU physics computing and multithreaded trials.
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
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "hdf5.h"

/* Bit masks to distinguish between two types of user-defined logging messages. */
#define USER_ADMIN  0x00000001
#define USER_SENSOR 0x00000002
#define USER_TARGET 0x00000004
#define USER_ALL    0x0FFFFFFF

/* Radar targets spread uniformly about the map */
#define NUM_TARGETS 1000

/* Sensor update interval, seconds */
const float dwell_time_s = 0.04f;

/* Radar wavelength. S-band, ~3 GHz, like the AN/APY-2. The multipath
 * lobe spacing in target altitude is λ × R / (2 × h_sensor); change
 * the wavelength and the lobe pattern scales linearly. */
const float radar_wavelength_m = 0.1f;


/* Antenna 3 dB beamwidth in azimuth. ~1.4 degrees for an AN/APY-2-like
 * search radar. The kernel uses a Gaussian main lobe whose 3 dB
 * half-width matches beamwidth_r/2, plus a -30 dB sidelobe floor. */
const float sensor_beam_width_d = 1.4f;
const float sensor_beam_width_r = (float)(1.4f * M_PI / 180.0);

/* Radar parameters that affect the clutter+CFAR detection model.
 * Identical structure and defaults to the GPU version in tut_5_2.h. */
struct radar_params {
    float range_res_m;
    int   n_pulses_per_dwell;
    int   cfar_n_ref;
    int   cfar_n_guard;
    float cfar_alpha;
    float noise_floor_norm;
    int   cell_grid_n_range;
    int   cell_grid_n_cross;
};

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

/* Synthetic terrain generation parameters */
const float terrain_max = 2500.0f;
const unsigned int terrain_octaves = 6u;
const float terrain_initfreq = 1.0f / 100000.0f;
const float terrain_ridginess = 1.2f;
const float terrain_peakiness = 1.7f;
const float terrain_stddev = 3.0f;  /* Local small-scale noise */

/* Refraction parameters */
const float atm_N0_units    = 313.0f;    /* sea-level refractivity, N-units */
const float atm_scale_h_m   = 7000.0f;   /* refractivity scale height, m */

/* Biomes: urban/fields, forest belt, bare mountain */
#define NBIOMES 3
/*
 * Biome parameters. biome_gamma_db[i] is the constant-gamma clutter
 * reflectivity in dB for biome i, per the Barton/Morchin model
 *   sigma_0 = gamma * sin(grazing_angle)
 * Typical values: smooth surface around -25 dB, crops/grass -15 dB,
 * rough terrain -5 dB. The biome is selected by terrain altitude at
 * each clutter sample point; the elevations array splits the terrain
 * into NBIOMES bands.
 *
 * Must match the values in tut_5_2.h to keep CPU and GPU physics aligned.
 */
const float biome_gamma_db[NBIOMES]        = { -20.0f, -15.0f, -5.0f };
const float biome_elevations[NBIOMES - 1]  = { 400.0f, 1000.0f };

/* biome_gamma_db converted to linear power ratio. Filled once at startup
 * to avoid repeated powf(10, .../10) inside the detection loop. */
static float biome_gamma_lin[NBIOMES] = { 0.0f, 0.0f, 0.0f };

/* Per-biome specular reflection coefficient |Γ_max|, at the smooth-surface
 * limit (zero roughness). The Rayleigh roughness penalty (computed
 * inline in multipath_gain) reduces the effective |Γ| as grazing
 * angle and surface roughness grow.
 *
 * Tutorial values:
 *   Biome 0 (lowlands < 400 m): valley floors, possible water/lakebed/paved
 *     ground. Smooth, strong specular reflection.
 *   Biome 1 (mid 400-1000 m): crops, grass, light vegetation. Moderate.
 *   Biome 2 (highlands > 1000 m): rough mountain. Specular nearly gone. */
const float biome_specular_max[NBIOMES] = { 0.90f, 0.50f, 0.10f };

/* Per-biome surface RMS height roughness in meters. Combines with
 * grazing angle in the Rayleigh criterion to suppress specular
 * reflection on rough surfaces.
 *
 * Note on biome 0: the small value here (5 cm) represents nearly
 * specular surfaces — water, dry lakebeds, salt flats, paved areas.
 * Real "lowland" terrain has more variation, but at S-band (λ=10 cm)
 * even a few decimeters of RMS roughness at moderate grazing angles
 * kills the coherent reflection completely. The tutorial value lets
 * us show lobing where lobing realistically happens (over smooth
 * surfaces); for "crops/grass" lowlands, use ~0.3 m and multipath
 * effectively vanishes, as it does in reality. */
const float biome_roughness_m[NBIOMES] = { 0.05f, 0.3f, 2.0f };

/* For ParaView visualization */
const char *terrain_h5name = "terrain.vtkhdf";
const char *events_h5name = "events.vtkhdf";
/* Terrain decimation factor */
const unsigned int stride_step = 32u;
/* Animation frame interval, seconds */
double frame_interval_s = 1.0;

struct target;
struct platform_state;
struct vtkhdf_handle;

struct vtkhdf_handle *vtkhdf_create(void);
void vtkhdf_destroy(struct vtkhdf_handle *h);

void events_vtkhdf_init(struct vtkhdf_handle *h, const char *filename, int n_targets);
void events_vtkhdf_close(const struct vtkhdf_handle *h);
void events_vtkhdf_append(struct vtkhdf_handle *h,
                   const struct target *targets,
                   const struct platform_state *awacs,
                   float sensor_dir,
                   float time_val);
void terrain_vtkhdf_write(const char *h5_filename, const float *map,
                          unsigned cols, unsigned rows,
                          float x_scale, float y_scale);

/* Utility function for user entertainment */
void progress_bar_update(unsigned int current, unsigned int total, time_t start_time);

/*******************************************************************************
 * The terrain model, altitudes in meters referred to a Cartesian plane touching
 * Earth at the reference position.
 */
struct terrain {
    float ref_lat_r;    /* Location of terrain reference point, radians latitude */
    float ref_lon_r;    /* Location of terrain reference point, radians longitude */
    float x_scale;      /* Meters per arcsecond longitude */
    float y_scale;      /* Meters per arcsecond latitude */
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    unsigned int cols;
    unsigned int rows;
    float *map;         /* The cols x rows matrix of elevations spaced by one arcsecond */
    int p[512];         /* The Perlin blueprint */
};

/* Allocate memory for the terrain object */
struct terrain *terrain_create(void)
{
    struct terrain *tp = malloc(sizeof(struct terrain));
    cmb_assert_release(tp != NULL);

    tp->cols = 0;
    tp->rows = 0;
    tp->map = NULL;

    return tp;
}

/* Simple fade function for smooth transitions: 6t^5 - 15t^4 + 10t^3 */
static float terrain_fade(const float t)
{
    return t * t * t * (t * (t * 6 - 15) + 10);
}

/* Linear interpolation */
static float terrain_lerp(const float t, const float a, const float b)
{
    return a + t * (b - a);
}

/* A simple hash to turn grid coordinates into a gradient direction */
static float terrain_grad(const int hash, const float x, const float y)
{
    const int h = hash & 15;
    const float u = h < 8 ? x : y;
    const float v = h < 4 ? y : h == 12 || h == 14 ? x : 0;

    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

/* Generate, shuffle, and duplicate the Perlin blueprint */
static void terrain_generate_blueprint(struct terrain *tp)
{
    for (int i = 0; i < 256; i++) {
        tp->p[i] = i;
    }

    for (int i = 255; i > 0; i--) {
        const int j = (int)cmb_random_uniform(0, i + 1);
        const int temp = tp->p[i];
        tp->p[i] = tp->p[j];
        tp->p[j] = temp;
    }

    for (int i = 0; i < 256; i++) {
        tp->p[256 + i] = tp->p[i];
    }
}

/* 2D Perlin noise function */
float terrain_perlin_noise2d(const struct terrain *tp, float x, float y)
{
    const int X = (int)floorf(x) & 255;
    const int Y = (int)floorf(y) & 255;

    x -= floorf(x);
    y -= floorf(y);

    const float u = terrain_fade(x);
    const float v = terrain_fade(y);

    const int A  = tp->p[X] + Y;
    const int AA = tp->p[A];
    const int AB = tp->p[A + 1];

    const int B  = tp->p[X + 1] + Y;
    const int BA = tp->p[B];
    const int BB = tp->p[B + 1];

    return terrain_lerp(v,
        terrain_lerp(u, terrain_grad(tp->p[AA], x, y),
                        terrain_grad(tp->p[BA], x - 1, y)),
        terrain_lerp(u, terrain_grad(tp->p[AB], x, y - 1),
                        terrain_grad(tp->p[BB], x - 1, y - 1)));
}

/*
 * Generate the terrain model relative to a flat plane using Perlin noise.
 * The plane touches the WGS84 ellipsoid at the reference location, given in degrees lat, lon.
 * The map is tsz_w nautical miles wide, tsw_h nautical miles tall.
 * Likely to take a while, so we entertain the user with a progress bar in the meanwhile.
 */
void terrain_initialize(struct terrain *tp,
                  const float tsz_w, const float tsz_h,
                  const float ref_lat, const float ref_lon)
{
    cmb_assert_release(tp != NULL);
    cmb_assert_release(tsz_w > 0);
    cmb_assert_release(tsz_h > 0);

    printf("\nInitializing terrain map of %3.0f x %3.0f nm centered on pos %5.2fN, %5.2fE\n",
            tsz_w, tsz_h, ref_lat, ref_lon);

    tp->cols = (unsigned int)(tsz_w * nm_to_meters / arcsec_to_meters);
    tp->rows = (unsigned int)(tsz_h * nm_to_meters / arcsec_to_meters);
    tp->ref_lat_r = (float)deg_to_rad * ref_lat;
    tp->ref_lon_r = (float)deg_to_rad * ref_lon;

    /* Map resolution one arcsecond, calculate as meters at ref pos on WGS84 ellipsoid */
    const double sin_lat = sinf(tp->ref_lat_r);
    const double cos_lat = cosf(tp->ref_lat_r);
    const double common = 1.0 - (WGS84_E2 * sin_lat * sin_lat);
    const double sqrt_common = sqrt(common);
    const double loc_radius_ew = WGS84_A / sqrt_common;
    const double m_per_deg_ew = loc_radius_ew * cos_lat * (M_PI / 180.0);
    tp->x_scale = (float)((1.0 / 3600.0) * m_per_deg_ew);
    const double loc_radius_ns = WGS84_A * (1.0 - WGS84_E2) / (common * sqrt_common);
    const double m_per_deg_ns = loc_radius_ns * (M_PI / 180.0);
    tp->y_scale = (float)((1.0 / 3600.0) * m_per_deg_ns);
    printf("Resolution x: %3.1f m, y: %3.1f m\n", tp->x_scale, tp->y_scale);

    const float x_span = (float)(tp->cols - 1) * tp->x_scale;
    const float y_span = (float)(tp->rows - 1) * tp->y_scale;
    tp->x_min = -(x_span / 2.0f);
    tp->x_max = (x_span / 2.0f);
    tp->y_min = -(y_span / 2.0f);
    tp->y_max = (y_span / 2.0f);

    printf("Generating %d x %d terrain grid...\n", tp->cols, tp->rows);
    tp->map = cmi_calloc(tp->cols * tp->rows, sizeof(float));

    const time_t start = time(NULL);
    struct cmb_datasummary tds = {};
    cmb_datasummary_initialize(&tds);

    terrain_generate_blueprint(tp);
    for (unsigned row = 0; row < tp->rows; row++) {
        /* Row is north-south (Y axis) */
        if (row % 100 == 0) {
            progress_bar_update(row, tp->rows, start);
        }
        const float ys = ((float)row - (float)tp->rows / 2.0f) * tp->y_scale;

        for (unsigned col = 0; col < tp->cols; col++) {
            /* Col is east-west (X axis) */
            const float xs = ((float)col - (float)tp->cols / 2.0f) * tp->x_scale;

            float h = 0.0f;
            float freq = terrain_initfreq;
            float amp = 1.0f;
            float weight = 1.0f;
            float ampsum = 0.0f;

            for (unsigned i = 0; i < terrain_octaves; i++) {
                float n = terrain_perlin_noise2d(tp, xs * freq, ys * freq);
                n = powf(1.0f - fabsf(n), terrain_ridginess);
                h += n * amp * weight;
                weight = n;
                freq *= 2.05f;
                ampsum += amp;
                amp  *= 0.5f;
            }

            h /= ampsum;
            h = powf(h, terrain_peakiness);
            float h_sum = (h * terrain_max) + (float)cmb_random_normal(0.0, terrain_stddev);
            h_sum = (h_sum < 0.0f) ? 0.0f : h_sum;
            tp->map[row * tp->cols + col] = h_sum;

            cmb_datasummary_add(&tds, h_sum);
        }
    }

    /* End progress bar at 100 % exactly */
    progress_bar_update(tp->rows, tp->rows, start);

    printf("\nTerrain characteristics:\n");
    printf("\tNumber of points:   %4.0f million\n", (float)cmb_datasummary_count(&tds)/1.0e6);
    printf("\tMinimum elevation:  %4.0f meters\n", cmb_datasummary_min(&tds));
    printf("\tMaximum elevation:  %4.0f meters\n", cmb_datasummary_max(&tds));
    printf("\tAverage elevation:  %4.0f meters\n", cmb_datasummary_mean(&tds));
    printf("\tStandard deviation: %4.0f meters\n", cmb_datasummary_stddev(&tds));
    printf("\tSize in memory:     %4.2f GB\n",
              (float)((size_t)tp->cols * (size_t)tp->rows * sizeof(float)) / 1024.0f / 1024.0f / 1024.0f);
    cmb_datasummary_terminate(&tds);

    printf("Writing decimated terrain map to file %s...", terrain_h5name);
    fflush(stdout);
    terrain_vtkhdf_write(terrain_h5name, tp->map, tp->cols, tp->rows, tp->x_scale, tp->y_scale);
    printf(" done\n");
}

void terrain_terminate(struct terrain *tp)
{
    cmb_assert_release(tp != NULL);
    cmb_assert_release(tp->map != NULL);

    cmi_free(tp->map);
    tp->map = NULL;
}

void terrain_destroy(struct terrain *tp)
{
    cmb_assert_release(tp != NULL);
    cmb_assert_release(tp->map == NULL);

    cmi_free(tp);
}

/* Lookup function to find the terrain array index corresponding to a given (x,y) position */
unsigned terrain_index(const struct terrain *tp, const float x, const float y)
{
    cmb_assert_release(tp != NULL);
    cmb_assert_release((x >= tp->x_min) && (x <= tp->x_max));
    cmb_assert_release((y >= tp->y_min) && (y <= tp->y_max));

    const int raw_col = (int)roundf(x / tp->x_scale) + (int)(tp->cols / 2);
    const int raw_row = (int)roundf(y / tp->y_scale) + (int)(tp->rows / 2);

    const unsigned col = (unsigned)((raw_col < 0) ? 0 : (raw_col >= (int)tp->cols ? (int)tp->cols - 1 : raw_col));
    const unsigned row = (unsigned)((raw_row < 0) ? 0 : (raw_row >= (int)tp->rows ? (int)tp->rows - 1 : raw_row));

    const unsigned index = row * tp->cols + col;
    cmb_assert_debug(index < tp->cols * tp->rows);

    return index;
}

/*
 * The stored value at grid cell (col, row) represents the terrain elevation
 * at the cell *center*, whose world coordinates are
 *     x = (col - cols/2) * x_scale
 *     y = (row - rows/2) * y_scale
 * Bilinear interpolation between four neighboring cell centers gives a
 * continuous elevation field over the entire map. Out-of-bounds sample
 * positions are clamped to the map edge, matching the cudaAddressModeClamp
 * behavior of the GPU texture.
 *
 * The interpolation arithmetic mirrors what the CUDA texture unit does
 * internally with cudaFilterModeLinear, except in IEEE 754 single precision
 * rather than the texture unit's 8-bit fractional fixed point. Results
 * will agree to within a few ULP except very near sharp terrain features.
 */

float terrain_elevation(const struct terrain *tp, const float x, const float y)
{
    cmb_assert_release(tp != NULL);

    /* World position to fractional cell coordinates. The -0.5f accounts
     * for the fact that cell (c, r) is centered at world position
     * ((c - cols/2) * x_scale, (r - rows/2) * y_scale): a position
     * exactly at a cell center maps to an integer (u, v). */
    const float u = (x / tp->x_scale) + (float)(tp->cols / 2u);
    const float v = (y / tp->y_scale) + (float)(tp->rows / 2u);

    /* Lower-left corner of the 2x2 patch we will interpolate within.
     * Clamp so the +1 neighbors below also stay in range. */
    const int c_max = (int)tp->cols - 1;
    const int r_max = (int)tp->rows - 1;

    int c0 = (int)floorf(u);
    int r0 = (int)floorf(v);
    if (c0 < 0)       c0 = 0;
    if (c0 > c_max-1) c0 = c_max - 1;
    if (r0 < 0)       r0 = 0;
    if (r0 > r_max-1) r0 = r_max - 1;
    const int c1 = c0 + 1;
    const int r1 = r0 + 1;

    /* Fractional offsets within the patch, in [0, 1]. */
    const float fx = fmaxf(0.0f, fminf(1.0f, u - (float)c0));
    const float fy = fmaxf(0.0f, fminf(1.0f, v - (float)r0));

    /* Four neighboring cell-center elevations. */
    const float h00 = tp->map[(unsigned)r0 * tp->cols + (unsigned)c0];
    const float h10 = tp->map[(unsigned)r0 * tp->cols + (unsigned)c1];
    const float h01 = tp->map[(unsigned)r1 * tp->cols + (unsigned)c0];
    const float h11 = tp->map[(unsigned)r1 * tp->cols + (unsigned)c1];

    /* Bilinear blend: interpolate along x at both y rows, then along y. */
    const float h0 = h00 + fx * (h10 - h00);
    const float h1 = h01 + fx * (h11 - h01);
    return h0 + fy * (h1 - h0);
}

/****************************************************************************
 * Radar targets spread over the map surface
 */
enum target_mode {
    HIDING,
    STAGING,
    FIRING,
    DRIVING
};

enum target_detect_state {
    UNDETERMINED,
    BEYOND_HORIZON,
    NADIR_HOLE,
    TERRAIN_SHIELDED,
    MISSED,
    DETECTED
};

struct target {
    /* Active process parent class */
    struct cmb_process core;
    /* Inherent parameters */
    float rcs_m2[4];
    float state_time_s[4];
    float height_m;
    struct terrain *terrain;
    /* Instantaneous state */
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
    tgt->alt_m = terrain_elevation(terp, tgt->x_m, tgt->y_m) + tgt->height_m;

    /* Set initial status probabilistically */
    const double ph = tgt->state_time_s[HIDING] / (tgt->state_time_s[HIDING] + tgt->state_time_s[DRIVING]);
    tgt->mode = (cmb_random_bernoulli(ph)) ? HIDING : DRIVING;
    tgt->tds = UNDETERMINED;

    // ReSharper disable once CppDFAEndlessLoop
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

/* Position update function, called by sensor */
static void target_position_update(struct target *tgt)
{
    cmb_assert_release(tgt != NULL);

    if (tgt->vel_ms > 0.0f) {
        const double t = cmb_time();
        const double dt = t - tgt->time_s;
        float x = tgt->x_m + (float)(dt * tgt->vel_ms * cosf(tgt->dir_r));
        if (x > tgt->terrain->x_max) {
            const float overshoot = x - tgt->terrain->x_max;
            x = tgt->terrain->x_min + overshoot;
        }
        else if (x < tgt->terrain->x_min) {
            const float overshoot = tgt->terrain->x_min - x;
            x = tgt->terrain->x_max - overshoot;
        }

        float y = tgt->y_m + (float)(dt * tgt->vel_ms * sinf(tgt->dir_r));
        if (y > tgt->terrain->y_max) {
            const float overshoot = y - tgt->terrain->y_max;
            y = tgt->terrain->y_min + overshoot;
        }
        else if (y < tgt->terrain->y_min) {
            const float overshoot = tgt->terrain->y_min - y;
            y = tgt->terrain->y_max - overshoot;
        }

        const float alt = terrain_elevation(tgt->terrain, x, y) + tgt->height_m;

        tgt->time_s = (float)t;
        tgt->x_m = x;
        tgt->y_m = y;
        tgt->alt_m = alt;
    }
}

/*
 * Effective Earth-radius factor k at altitude h, for an atmosphere with
 * exponentially decaying refractivity. Returns 1.0 in vacuum (no
 * refraction), ≈ 1.33 at sea level for standard atmosphere, → 1.0 at
 * very high altitude.
 *
 * Derivation: a ray traveling horizontally at altitude h has curvature
 * radius R_ray = -1 / (dn/dh). The standard "effective Earth" trick is
 * to rescale the Earth's radius by k so that ray and Earth share a
 * common reference frame, giving k = 1 / (1 + R_earth * dn/dh).
 */
static float atmosphere_k_eff(const float h_m, const float r_earth_m)
{
    const float dn_dh = -(atm_N0_units * 1.0e-6f / atm_scale_h_m)
                        * expf(-h_m / atm_scale_h_m);
    return 1.0f / (1.0f + r_earth_m * dn_dh);
}

/*
 * Detection pipeline 1: Test whether a target is inside the beam's
 * *effective gate width* —  * the maximum of the antenna beamwidth
 * and the rotation per dwell.
 *
 * Gating at just the 3 dB beamwidth would leave small angular gaps
 * between consecutive dwells whenever rot_inc > beamwidth. Targets
 * that fall in such gaps would never get a detection attempt despite
 * the beam's sidelobes (and atmospheric reality) putting real signal
 * on them. Widening the gate to encompass at least one rotation step
 * closes that gap; the antenna_gain_sq function still attenuates
 * targets at large offsets correctly via the Gaussian + sidelobe-floor
 * model, so off-beam targets simply get very low Pd rather than being
 * skipped.
 */
static bool target_is_in_beam(const float tgt_azi,
                              const float beam_dir,
                              const float beamwidth_r,
                              const float rot_inc_r)
{
    const float half_width = 0.5f * fmaxf(beamwidth_r, rot_inc_r);

    float rel_azi = tgt_azi - beam_dir;
    while (rel_azi >  (float)M_PI) rel_azi -= 2.0f * (float)M_PI;
    while (rel_azi < -(float)M_PI) rel_azi += 2.0f * (float)M_PI;
    return (fabsf(rel_azi) <= half_width);
}

/*
 * Detection pipeline 2: Radar horizon check with altitude-dependent
 * refraction.
 *
 * Closed-form approximation using k(h) evaluated at the arithmetic
 * mean of sensor and target altitudes. Exact integration would compute
 * the ray-drop integral along the path, but for our geometries (AWACS
 * + ground target, near-horizontal rays) the mean-altitude
 * approximation is accurate to better than 1 % of horizon distance.
 */
static bool target_is_beyond_horizon(const float d_2d, const float h_sensor,
                                     const float h_tgt, const float r_earth)
{
    const float hs = fmaxf(0.0f, h_sensor);
    const float ht = fmaxf(0.0f, h_tgt);

    const float h_avg = 0.5f * (hs + ht);
    const float k_eff = atmosphere_k_eff(h_avg, r_earth);
    const float r_eff = k_eff * r_earth;

    const float max_dist = sqrtf(2.0f * r_eff * hs) + sqrtf(2.0f * r_eff * ht);

    return (d_2d > max_dist);
}


/*
 * Detection pipeline 3: Nadir hole check with platform roll compensation,
 * also checking if target for some reason is above the vertical lobe sector,
 * even if that is not relevant in this scenario.
 */
static bool target_is_outside_vertical(const float dx, const float dy, const float dz,
                                       const float d_2d, const float platform_hdg,
                                       const float platform_roll,
                                       const float min_elev, const float max_elev)
{
    const float tgt_azi = atan2f(dy, dx);
    const float rel_brg = tgt_azi - platform_hdg;
    const float geom_elev = atan2f(dz, d_2d);
    const float apparent_elev = geom_elev - (platform_roll * sinf(rel_brg));

    return ((apparent_elev < min_elev) || (apparent_elev > max_elev));
}

/*
 * Detection pipeline 4: Terrain ray-marching with altitude-dependent
 * refraction.
 *
 * The straight line from target to sensor is the chord. The actual
 * refracted ray sags below the chord by the "Earth bulge correction,"
 * scaled by the local k(h). At each step along the chord we compute
 *   bulge(s) = s * (d_2d - s) / (2 * k(h_ray) * R_earth)
 * and check the corrected ray altitude against the terrain at the
 * step's world position.
 *
 * Using k evaluated at the *current step's* straight-line altitude is
 * a one-shot approximation that avoids iterating on the implicit
 * coupling between h_ray and k(h_ray). For our altitudes the
 * difference from a fully iterative solution is sub-meter.
 *
 * The Earth-curvature term (the 1/R contribution to the bulge) is
 * always positive; the refraction term is always negative; their sum
 * is positive for any plausible atmosphere, so the corrected ray is
 * always below the chord. This means the new check is at least as
 * permissive as the un-refracted line of sight, never more so.
 */
static bool target_is_terrain_shielded(const float sx, const float sy, const float sa,
                                       const float tx, const float ty, const float ta,
                                       const struct terrain *terp,
                                       const float r_earth)
{
    const float dx = sx - tx;
    const float dy = sy - ty;
    const float dz = sa - ta;
    const float d_2d = sqrtf(dx * dx + dy * dy);

    const float step_size = fminf(terp->x_scale, terp->y_scale) * 0.5f;
    const int num_steps = (int)(d_2d / step_size);
    if (num_steps < 1) {
        return false;
    }

    const float inv_steps = 1.0f / (float)num_steps;
    const float two_r_earth = 2.0f * r_earth;

    /* March from target toward sensor: i=1 is just past the target. */
    for (int i = 1; i < num_steps; i++) {
        const float t = (float)i * inv_steps;

        float cx = tx + dx * t;
        float cy = ty + dy * t;
        const float ca_chord = ta + dz * t;

        cx = fmaxf(terp->x_min, fminf(cx, terp->x_max));
        cy = fmaxf(terp->y_min, fminf(cy, terp->y_max));

        /* Bulge correction. s is arc length from target along the
         * chord (≈ 2D distance for small elevation angles). k is
         * evaluated at the chord altitude rather than the corrected
         * altitude — fixed-point iteration would change the answer
         * by less than a meter. */
        const float s = t * d_2d;
        const float s_rem = d_2d - s;
        const float k_local = atmosphere_k_eff(ca_chord, r_earth);
        const float bulge = (s * s_rem) / (two_r_earth * k_local);
        const float ca_ray = ca_chord - bulge;

        const float terr_alt = terrain_elevation(terp, cx, cy);
        if (ca_ray < terr_alt) {
            return true;
        }
    }

    return false;
}

/*
 * Antenna pattern gain (one-way) squared, evaluated at angular offset
 * from beam center. Gaussian main lobe with 3 dB half-width matching
 * beamwidth_r/2, plus a -30 dB sidelobe floor so targets at large
 * angular offsets don't disappear entirely.
 *
 * For a Gaussian G(theta) = exp(-theta^2 / sigma^2) with 3 dB point at
 * theta = beamwidth/2, we have sigma = beamwidth / (2 * sqrt(ln 2)).
 * One-way gain squared = exp(-2 * ln(2) * (2*theta/beamwidth)^2).
 */
static float antenna_gain_sq(const float angular_offset_r,
                             const float beamwidth_r)
{
    const float u    = 2.0f * angular_offset_r / beamwidth_r;
    const float g_sq = expf(-2.0f * 0.6931472f * u * u);
    return fmaxf(g_sq, 1.0e-3f);    /* -30 dB sidelobe floor */
}

/*
 * Look up biome gamma (linear) from terrain altitude at a sample point.
 */
static float biome_gamma_at_alt(const float alt_m)
{
    float g = biome_gamma_lin[NBIOMES - 1];
    for (unsigned int b = 0; b < NBIOMES - 1; b++) {
        if (alt_m < biome_elevations[b]) {
            g = biome_gamma_lin[b];
            break;
        }
    }
    return g;
}

/*
 * Integrate clutter contribution over one resolution cell. The cell is
 * an annular wedge between (center_range - dr/2) and (center_range + dr/2),
 * angular width beamwidth_r, centered on beam_dir.
 *
 * Returns the cell's total normalized return power, including range
 * attenuation 1/R^4 and antenna gain G^2 at each sample.
 *
 * Mirrors the GPU's integrate_cell_clutter exactly. For tutorial
 * purposes the result is in arbitrary normalized units; what matters
 * is the ratio with the target signal and the CFAR threshold.
 */
static float integrate_cell_clutter(const float center_range,
                                    const float beam_dir,
                                    const float beamwidth_r,
                                    const float range_res_m,
                                    const float sx, const float sy,
                                    const float sa,
                                    const int n_range, const int n_cross,
                                    const struct terrain *terp,
                                    const float r_earth)
{
    const float dr = range_res_m;
    const float dtheta = beamwidth_r;
    const float dA_per_unit = (dr * dtheta) / ((float)(n_range * n_cross));

    float clutter = 0.0f;

    for (int i = 0; i < n_range; i++) {
        const float t_r = ((float)i + 0.5f) / (float)n_range;
        const float R = center_range - 0.5f * dr + t_r * dr;
        const float R_safe = fmaxf(1.0f, R);
        const float inv_R4 = 1.0f / (R_safe * R_safe * R_safe * R_safe);

        for (int j = 0; j < n_cross; j++) {
            const float t_c = ((float)j + 0.5f) / (float)n_cross;
            const float dtheta_off = -0.5f * beamwidth_r + t_c * beamwidth_r;
            const float az = beam_dir + dtheta_off;

            const float wx = sx + R_safe * cosf(az);
            const float wy = sy + R_safe * sinf(az);
            const float wx_c = fmaxf(terp->x_min, fminf(wx, terp->x_max));
            const float wy_c = fmaxf(terp->y_min, fminf(wy, terp->y_max));

            const float terr = terrain_elevation(terp, wx_c, wy_c);

            /* Earth-curvature drop at this range. Using fixed k=4/3
             * (rather than altitude-dependent k) is a small simplification
             * for the clutter integration; the bulk geometry through the
             * lower atmosphere dominates. */
            const float earth_drop = (R_safe * R_safe)
                                   / (2.0f * 1.33f * r_earth);
            const float h_sensor_eff = sa - terr - earth_drop;
            if (h_sensor_eff <= 0.0f) continue;   /* sample below horizon */

            const float sin_graze = fminf(1.0f, h_sensor_eff / R_safe);

            const float gamma   = biome_gamma_at_alt(terr);
            const float sigma_0 = gamma * sin_graze;

            const float g_sq = antenna_gain_sq(dtheta_off, beamwidth_r);

            const float dA = R_safe * dA_per_unit;

            clutter += sigma_0 * g_sq * dA * inv_R4;
        }
    }

    return clutter;
}

/*
 * CA-CFAR threshold. Integrate clutter over 2 * n_ref reference cells,
 * skipping n_guard cells on each side of the test cell. Multiply the
 * mean by cfar_alpha to set the threshold.
 *
 * Returns the threshold *energy* in the same units as the target
 * signal and test-cell clutter.
 */
static float cfar_threshold(const float target_range,
                            const float beam_dir,
                            const float sx, const float sy, const float sa,
                            const struct radar_params *radar,
                            const float beamwidth_r,
                            const struct terrain *terp,
                            const float r_earth)
{
    const float dr = radar->range_res_m;
    const int n_ref = radar->cfar_n_ref;
    const int n_guard = radar->cfar_n_guard;

    float sum = 0.0f;
    int n_used = 0;

    for (int k = 1; k <= n_ref; k++) {
        const int range_offset_cells = n_guard + k;

        /* Reference cell on the far side. */
        const float R_far = target_range + (float)range_offset_cells * dr;
        sum += integrate_cell_clutter(R_far, beam_dir, beamwidth_r, dr,
                                       sx, sy, sa,
                                       radar->cell_grid_n_range,
                                       radar->cell_grid_n_cross,
                                       terp, r_earth);
        n_used++;

        /* Reference cell on the near side. Skip if it would fall below
         * 1 km (the radar's near-range cutoff). */
        const float R_near = target_range - (float)range_offset_cells * dr;
        if (R_near > 1000.0f) {
            sum += integrate_cell_clutter(R_near, beam_dir, beamwidth_r, dr,
                                           sx, sy, sa,
                                           radar->cell_grid_n_range,
                                           radar->cell_grid_n_cross,
                                           terp, r_earth);
            n_used++;
        }
    }

    const float mean_clutter = (n_used > 0) ? (sum / (float)n_used) : 0.0f;
    return radar->cfar_alpha * (mean_clutter + radar->noise_floor_norm);
}

/*
 * Two-way multipath interference factor for the target signal.
 *
 * Returns a multiplier in [0, ~16] to be applied to E_target. A value
 * of 1 means no multipath effect (rough biome or invalid geometry);
 * values above 1 mean constructive interference, below 1 destructive.
 *
 * The specular point is iterated once: an initial estimate using flat
 * ground at altitude 0, then refined using the actual ground altitude
 * at that estimate. Two iterations would change the result by sub-meter
 * lobe shifts. The helper assumes the bounce path is unobstructed; for
 * the typical AWACS-vs-ground-target geometry the direct and bounce
 * paths run through nearly the same airspace, so if the direct path
 * passed the terrain-shielding check, the bounce path is almost
 * certainly clear.
 */
static float multipath_gain(const float sx, const float sy, const float sa,
                            const float tx, const float ty, const float ta,
                            const float d_2d,
                            const struct terrain *terp)
{
    /* First-pass specular point assuming ground at altitude 0. */
    const float t0 = sa / (sa + ta);
    float spec_x = sx + (tx - sx) * t0;
    float spec_y = sy + (ty - sy) * t0;

    /* Clamp inside map bounds before sampling terrain. */
    spec_x = fmaxf(terp->x_min, fminf(spec_x, terp->x_max));
    spec_y = fmaxf(terp->y_min, fminf(spec_y, terp->y_max));

    float ground_alt = terrain_elevation(terp, spec_x, spec_y);

    /* Re-derive the specular point above the actual ground. If the
     * ground is above either endpoint, the specular geometry is
     * degenerate — fall back to no multipath. */
    const float h_s_eff = sa - ground_alt;
    const float h_t_eff = ta - ground_alt;
    if (h_s_eff <= 1.0f || h_t_eff <= 1.0f) {
        return 1.0f;
    }
    const float t1 = h_s_eff / (h_s_eff + h_t_eff);
    spec_x = sx + (tx - sx) * t1;
    spec_y = sy + (ty - sy) * t1;
    spec_x = fmaxf(terp->x_min, fminf(spec_x, terp->x_max));
    spec_y = fmaxf(terp->y_min, fminf(spec_y, terp->y_max));
    ground_alt = terrain_elevation(terp, spec_x, spec_y);

    const float h_s = sa - ground_alt;
    const float h_t = ta - ground_alt;
    if (h_s <= 1.0f || h_t <= 1.0f) {
        return 1.0f;
    }

    /* Path length difference, exact small-angle form. The direct path
     * goes from (sx, sy, sa) to (tx, ty, ta); the bounce path goes
     * sensor -> specular -> target. Use d_2d for the horizontal
     * baseline since we have it. */
    const float d_direct = sqrtf(d_2d * d_2d + (h_s - h_t) * (h_s - h_t));
    const float d_b1 = sqrtf((t1 * d_2d) * (t1 * d_2d) + h_s * h_s);
    const float d_b2 = sqrtf(((1.0f - t1) * d_2d) * ((1.0f - t1) * d_2d)
                              + h_t * h_t);
    const float d_bounce = d_b1 + d_b2;
    const float delta_d = d_bounce - d_direct;

    /* Grazing angle at the specular point; both legs share it. */
    const float sin_graze = h_s / fmaxf(1.0f, d_b1);

    /* Biome parameters at the specular point. */
    float gamma_max = biome_specular_max[NBIOMES - 1];
    float sigma_h   = biome_roughness_m[NBIOMES - 1];
    for (unsigned int b = 0; b < NBIOMES - 1; b++) {
        if (ground_alt < biome_elevations[b]) {
            gamma_max = biome_specular_max[b];
            sigma_h   = biome_roughness_m[b];
            break;
        }
    }

    /* Rayleigh roughness suppression: rougher surface or steeper grazing
     * scatters the specular component into diffuse, reducing |Γ_eff|.
     *   |Γ_eff| = |Γ_max| × exp(-(4π σ_h sin ψ / λ)²)
     */
    const float roughness_arg = (4.0f * (float)M_PI * sigma_h * sin_graze)
                              / radar_wavelength_m;
    const float gamma_eff = gamma_max * expf(-roughness_arg * roughness_arg);

    if (gamma_eff < 0.01f) {
        return 1.0f;        /* multipath effectively absent */
    }

    /* Phase difference, with the π flip on reflection. */
    const float phase = 2.0f * (float)M_PI * delta_d / radar_wavelength_m
                      + (float)M_PI;

    /* One-way interference factor. */
    const float g_one_way = 1.0f + gamma_eff * gamma_eff
                          + 2.0f * gamma_eff * cosf(phase);

    /* Two-way (round-trip) factor. Clamp the lower end to avoid
     * exactly-zero target energy at perfect nulls — physically there
     * is always some residual signal from non-ideal geometry. */
    const float g_two_way = g_one_way * g_one_way;
    return fmaxf(g_two_way, 1.0e-3f);
}

/*
 * Detection pipeline 5. Probabilistic detection in ground clutter using:
 *   1. Antenna pattern gain on the target (Gaussian + sidelobe floor)
 *   2. Non-coherent pulse integration boost on the target signal
 *   3. Spatial clutter integration over the target's resolution cell
 *   4. CA-CFAR threshold from 16 reference range cells
 *   5. Logistic-curve smoothing over the test/threshold margin
 *
 * Returns true if the target is detected this dwell. Mirrors the GPU's
 * probabilistic_detection function.
 */
static bool target_attempt_detection(const float dx, const float dy,
                                     const float dz,
                                     const float d_3d, const float d_2d,
                                     const float sx, const float sy,
                                     const float sa,
                                     const float beam_dir,
                                     const float beamwidth_r,
                                     const float ref_range, const float ref_rcs,
                                     const float tcx,
                                     const struct radar_params *radar,
                                     const struct terrain *terp,
                                     const float r_earth)
{
    const float r = fmaxf(1.0f, d_3d);
    const float target_az = atan2f(dy, dx);

    /* Angular offset of target from beam center; wrap to (-pi, pi]. */
    float target_offset = target_az - beam_dir;
    while (target_offset >  (float)M_PI) target_offset -= 2.0f * (float)M_PI;
    while (target_offset < -(float)M_PI) target_offset += 2.0f * (float)M_PI;

    /* Antenna gain on the target. */
    const float g_target_sq = antenna_gain_sq(target_offset, beamwidth_r);

    /* Multipath interference: two-way factor on the target signal. */
    const float tx = sx + dx;
    const float ty = sy + dy;
    const float ta = sa + dz;
    const float g_mp = multipath_gain(sx, sy, sa, tx, ty, ta, d_2d, terp);

    /* Target signal energy: free-space SNR scaled by antenna gain^2,
     * pulse integration gain, and multipath. */
    const float range_ratio = ref_range / r;
    const float r2 = range_ratio * range_ratio;
    const float snr_base = r2 * r2 * (tcx / ref_rcs);

    const float pulse_int_gain = 0.7f * sqrtf((float)radar->n_pulses_per_dwell);
    const float E_target = snr_base * g_target_sq * pulse_int_gain * g_mp;

    /* Clutter in the test cell. (Clutter has its own multipath in
     * principle, but the spatial averaging over the resolution cell
     * smooths the lobing into roughly a constant factor; we leave
     * clutter unmodified for tutorial simplicity.) */
    const float E_clutter_test = integrate_cell_clutter(
        d_2d, beam_dir, beamwidth_r, radar->range_res_m,
        sx, sy, sa,
        radar->cell_grid_n_range, radar->cell_grid_n_cross,
        terp, r_earth);

    /* CFAR threshold from reference cells. */
    const float T = cfar_threshold(
        d_2d, beam_dir, sx, sy, sa,
        radar, beamwidth_r, terp, r_earth);

    const float test_cell = E_target + E_clutter_test + radar->noise_floor_norm;
    const float margin = (test_cell - T) / fmaxf(T, radar->noise_floor_norm);

    const float pd = 1.0f / (1.0f + expf(-3.0f * margin));

    return cmb_random_bernoulli(pd);
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
    float orbit_dist_m;    /* Length of entire orbit, meters */
    float orbit_time_s;    /* Duration of a full orbit, seconds */
    float side_multiplier; /* 1.0 for clockwise, -1.0 for anti-clockwise */
    float lat_r2m;         /* Local conversion radians to meters, latitude */
    float lon_r2m;         /* Local conversion radians to meters, longitude */
    float roll_angle_r;    /* Platform banking angle during turns, radians */
    float local_earth_radius_m;         /* Effective Earth radius allowing for radar diffraction */
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
    printf("Racetrack bank angle %3.1f deg\n", rt->roll_angle_r * rad_to_deg);
    printf("Racetrack orbit duration %4.0f seconds\n", rt->orbit_time_s);

    /* Headline horizon for a sea-level target, using k evaluated at the
     * mean of sensor and target altitudes — the same approximation used
     * by target_is_beyond_horizon at runtime. */
    const float h_avg_sealvl = 0.5f * rt->altitude_m;
    const float k_avg        = atmosphere_k_eff(h_avg_sealvl, rt->local_earth_radius_m);
    const float max_dist     = sqrtf(2.0f * k_avg * rt->local_earth_radius_m * rt->altitude_m);
    printf("Radar horizon at %3.0f nautical miles (sea level target)\n",
            max_dist / nm_to_meters);
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
    float beamwidth_r;
    float elev_angle_min_r;
    float elev_angle_max_r;
    float ref_range_m;
    float ref_rcs_m2;
    struct radar_params radar;

    struct vtkhdf_handle *hdf;
};

/*
 * The sensor cmb_process function
 */
void *sensor_proc(struct cmb_process *me, void *vctx)
{
    cmb_assert_release(me != NULL);
    cmb_assert_release(vctx != NULL);

    struct target *targets = vctx;
    struct sensor *senp = (struct sensor *)me;
    struct platform *host = senp->host;
    const struct racetrack *orbit = host->orbit;

    /* Antenna rotation per dwell. With 6 RPM and 40 ms dwell, this is
     * about 1.44 degrees per dwell. The beam itself has angular width
     * beamwidth_r (~1.4 degrees), so successive dwells slightly overlap. */
    const float rot_inc = (float)(senp->rpm * (dwell_time_s / 60.0f)
                                  * (2.0f * M_PI));

    while (true) {
        cmb_process_hold((double)dwell_time_s);

        const float prev_hdg = host->state.dir;
        platform_update(host);

        /* Advance the beam direction by one dwell's worth of rotation,
         * accounting for any platform yaw between calls. cur_dir_r is
         * the beam *center* at this dwell. */
        const float ddir = host->state.dir - prev_hdg;
        senp->cur_dir_r += rot_inc + ddir;
        while (senp->cur_dir_r >= 2.0f * (float)M_PI) {
            senp->cur_dir_r -= 2.0f * (float)M_PI;
        }
        while (senp->cur_dir_r < 0.0f) {
            senp->cur_dir_r += 2.0f * (float)M_PI;
        }

        const float beam_dir = senp->cur_dir_r;

        /* --- Target detection pipeline --- */
        for (int i = 0; i < NUM_TARGETS; i++) {
            struct target *tgt = &(targets[i]);
            target_position_update(tgt);

            const float sx = host->state.x;
            const float sy = host->state.y;
            const float sa = host->state.alt;
            const float tx = tgt->x_m;
            const float ty = tgt->y_m;
            const float ta = tgt->alt_m;
            const float dx = tx - sx;
            const float dy = ty - sy;
            const float dz = ta - sa;
            const float d_2d = sqrtf(dx * dx + dy * dy);
            const float d_3d = sqrtf(d_2d * d_2d + dz * dz);
            const float tgt_azi = atan2f(dy, dx);

            /* Stage 1: in-beam test, using the centered Gaussian beam. */
            if (!target_is_in_beam(tgt_azi, beam_dir, senp->beamwidth_r, rot_inc)) {
                continue;
            }

            /* Stage 2: horizon. */
            if (target_is_beyond_horizon(d_2d, sa, ta,
                                          orbit->local_earth_radius_m)) {
                tgt->tds = BEYOND_HORIZON;
                continue;
            }

            /* Stage 3: vertical lobe / nadir hole. */
            if (target_is_outside_vertical(dx, dy, dz, d_2d,
                                            host->state.dir, host->state.rol,
                                            senp->elev_angle_min_r,
                                            senp->elev_angle_max_r)) {
                tgt->tds = NADIR_HOLE;
                continue;
            }

            /* Stage 4: terrain raymarch. */
            if (target_is_terrain_shielded(sx, sy, sa, tx, ty, ta,
                                            tgt->terrain,
                                            orbit->local_earth_radius_m)) {
                tgt->tds = TERRAIN_SHIELDED;
                continue;
            }

            /* Stage 5: clutter / antenna pattern / pulse integration / CFAR. */
            /* Stage 5: clutter / antenna pattern / pulse integration / CFAR /
             *           multipath. */
            if (target_attempt_detection(dx, dy, dz, d_3d, d_2d,
                                          sx, sy, sa,
                                          beam_dir, senp->beamwidth_r,
                                          senp->ref_range_m, senp->ref_rcs_m2,
                                          tgt->rcs_now_m2,
                                          &senp->radar,
                                          tgt->terrain,
                                          orbit->local_earth_radius_m)) {
                tgt->tds = DETECTED;
                if (tgt->mode != FIRING) {
                    tgt->detected = true;
                }
                                          }
            else {
                tgt->tds = MISSED;
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
                       struct platform *host, void *vctx)
{
    cmb_assert_release(senp != NULL);
    cmb_assert_release(host != NULL);

    senp->host = host;
    senp->cur_dir_r = (float)(M_PI / 2.0);
    senp->rpm = rpm;
    senp->beamwidth_r = sensor_beam_width_r;
    senp->elev_angle_min_r = min_elev;
    senp->elev_angle_max_r = max_elev;
    senp->ref_range_m = ref_rng * (float)nm_to_meters;
    senp->ref_rcs_m2 = ref_rcs;

    /* Same defaults as the GPU version. */
    senp->radar.range_res_m        = 150.0f;
    senp->radar.n_pulses_per_dwell = 12;
    senp->radar.cfar_n_ref         = 8;
    senp->radar.cfar_n_guard       = 1;
    senp->radar.cfar_alpha         = 5.62f;     /* Pfa ~ 1e-6, N = 16 */
    senp->radar.noise_floor_norm   = 1.0f;
    senp->radar.cell_grid_n_range  = 4;
    senp->radar.cell_grid_n_cross  = 4;

    /* Convert biome_gamma_db to linear power ratio. Done here rather
     * than at file scope because powf isn't a constant expression. */
    for (int b = 0; b < NBIOMES; b++) {
        biome_gamma_lin[b] = powf(10.0f, biome_gamma_db[b] / 10.0f);
    }

    senp->hdf = vtkhdf_create();
    events_vtkhdf_init(senp->hdf, events_h5name, NUM_TARGETS);

    cmb_process_initialize((struct cmb_process *)senp, name, sensor_proc, vctx, 0);
}

void sensor_terminate(struct sensor *senp)
{
    events_vtkhdf_close(senp->hdf);
    vtkhdf_destroy(senp->hdf);
    cmb_process_terminate((struct cmb_process *)senp);
}

void sensor_destroy(struct sensor *senp)
{
    free(senp);
}

/*****************************************************************************
 * Our simulated world consists of these entities.
 */
struct simulation {
    struct platform *AWACS;
    struct target *targets;
    struct cmb_process *HDF5_output;
};

/*
 * A single trial is defined by these parameters and generates these results.
 */
struct trial {
    struct terrain *terrain;
    double dur_s;
    uint64_t seed_used;
    unsigned num_found;
};

/*
 * Event to close down the simulation.
 */
void end_sim(void *subject, void *object)
{
    cmb_unused(object);

    struct simulation *sim = subject;
    struct target *tgts = sim->targets;

    cmb_process_stop((struct cmb_process *)(sim->AWACS->radar), NULL);
    for (unsigned ui = 0; ui < NUM_TARGETS; ui++) {
        cmb_process_stop((struct cmb_process *)&tgts[ui], NULL);
    }

    cmb_process_stop(sim->HDF5_output, NULL);
}

const char *hhhmmss_formatter(double t);

/*
 * A cmb_process to display a progress bar during the simulation
 */
void *ent_proc(struct cmb_process *me, void *vtp)
{
    cmb_unused(me);

    const time_t started = time(NULL);
    const unsigned ncycles = 100u;
    const double stime_start = cmb_time();
    const double stime_end = *(double *)vtp;
    const double stime_incr = (stime_end - stime_start) / ncycles;

    for (unsigned ui = 1; ui <= ncycles; ui++) {
        cmb_process_hold(stime_incr);
        progress_bar_update(ui, ncycles, started);
    }

    return NULL;
}

/*
 * A cmb_process to write snapshots to HDF5 output file
 */
void *out_proc(struct cmb_process *me, void *vtp)
{
    cmb_unused(me);
    const struct simulation *sim = (struct simulation *)vtp;
    const struct target *tgts = sim->targets;
    struct platform *pfp = sim->AWACS;
    struct sensor *senp = pfp->radar;

    platform_update(pfp);
    events_vtkhdf_append(senp->hdf, tgts, &(pfp->state),
                         senp->cur_dir_r, (float)cmb_time());

    while (true) {
        cmb_process_hold(frame_interval_s);
        platform_update(pfp);
        events_vtkhdf_append(senp->hdf, tgts, &(pfp->state),
                         senp->cur_dir_r, (float)cmb_time());
    }
}

/*
 * The simulation driver function to execute one trial
 */
void run_trial(void *vtrl)
{
    cmb_assert_release(vtrl != NULL);
    struct trial *trl = vtrl;
    struct simulation sim = {};

    /* Set up our trial housekeeping, assuming random seed already set */
    printf("Initializing event queue\n");
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_logger_flags_off(USER_ALL);
    cmb_event_queue_initialize(0.0);
    cmb_logger_timeformatter_set(hhhmmss_formatter);

    /* Create and start the targets */
    printf("Initializing targets\n");
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
    printf("Initializing racetrack pattern\n");
    struct racetrack *orbit = racetrack_create();
    const float start_time = 0.0f;      /* Hours */
    const float anchor_lat = 30.0f;     /* Degrees */
    const float anchor_lon = -10.0f;    /* Degrees */
    const float orientation = 0.0f;     /* Degrees, nautical */
    const float length = 50.0f;         /* Nautical miles */
    const float turn_radius = 10.0f;    /* Nautical miles */
    const float flight_level = 310.0f;  /* Flight level */
    const float velocity = 300.0f;      /* Knots */
    const bool clockwise = true;
    racetrack_initialize(orbit, start_time, anchor_lat, anchor_lon, orientation,
                         length, turn_radius, flight_level, velocity, clockwise);
    racetrack_write_vtp(orbit, "racetrack.vtp");

    printf("Initializing AWACS platform\n");
    struct platform *awacs = platform_create();
    sim.AWACS = awacs;
    platform_initialize(awacs);
    awacs->orbit = orbit;
    /* Set initial position and direction coordinates */
    platform_update(awacs);

    printf("Initializing radar\n");
    struct sensor *radar = sensor_create();
    sim.AWACS->radar = radar;
    const float rpm = 6.0f;
    const float max_elev = (float)(60.0 * deg_to_rad);
    const float min_elev = (float)(-20.0 * deg_to_rad);
    const float ref_range = 150.0f;     /* Nautical miles */
    const float ref_rcs = 1.0f;
    sensor_initialize(radar, "Radar", rpm, min_elev, max_elev,
                      ref_range, ref_rcs, awacs, sim.targets);
    cmb_process_start((struct cmb_process *)radar);

    /* Process to write output events */
    printf("Starting output process\n");
    sim.HDF5_output = cmb_process_create();
    cmb_process_initialize(sim.HDF5_output, "HDF5 output", out_proc, &sim, -1);
    cmb_process_start(sim.HDF5_output);

    /* Schedule the simulation control events */
    printf("Scheduling end event\n");
    double t_end_s = trl->dur_s;
    cmb_event_schedule(end_sim, &sim, NULL, t_end_s, 0);

    /* Process to show the progress bar */
    struct cmb_process *entertainment = cmb_process_create();
    cmb_process_initialize(entertainment, "Progress bar", ent_proc, &t_end_s, 0);
    cmb_process_start(entertainment);

    /* Run this trial */
    printf("\nRunning simulation for %4.1f simulated hours...\n", t_end_s/3600.0);
    fflush(stdout);
    cmb_event_queue_execute();

    trl->num_found = 0;
    for (unsigned ui = 0; ui < NUM_TARGETS; ui++) {
        if (sim.targets[ui].detected) {
            trl->num_found++;
        }
    }

    printf("\nFound %d targets of %d total, detection rate %4.1f %%\n",
            trl->num_found, NUM_TARGETS,
            100.0 * ((double)trl->num_found) / (double)(NUM_TARGETS));

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

    cmb_process_terminate(entertainment);
    cmb_process_destroy(entertainment);

    /* Final housekeeping to leave everything as we found it */
    cmb_event_queue_terminate();
    cmb_random_terminate();
}

/*
 * The minimal single-threaded main function
 */
int main(int argc, char **argv)
{
    bool timing_enabled = false;
    uint64_t seed = cmb_random_hwseed();
    double dur_h = 6.0;

    int opt;
    while ((opt = getopt(argc, argv, "d:s:t")) != -1) {
        switch (opt) {
            case 'd':
                errno = 0;
                dur_h = strtof(optarg, NULL);
                if (errno != 0 || dur_h <= 0.0) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            case 's':
                errno = 0;
                seed = (uint64_t)strtoull(optarg, NULL, 0);
                if (errno != 0 || seed == 0u) {
                    fprintf(stderr, "Invalid argument %s\n", optarg);
                    abort();
                }
                break;
            case 't':
                timing_enabled = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-s <seed>][-t]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    const clock_t start_time = clock();

    /* Allocate and generate the terrain map to be used for several trials */
    const float fwidth_nm = 1000.0f;
    const float fheight_nm = 1000.0f;
    const float ref_lat = 30.0f;
    const float ref_lon = -10.0f;

    struct trial trl = {};

    printf("Initializing random generator\n");
    trl.seed_used = seed;
    cmb_random_initialize(trl.seed_used);

    struct terrain *tp = terrain_create();
    terrain_initialize(tp, fwidth_nm, fheight_nm, ref_lat, ref_lon);
    trl.terrain = tp;
    trl.dur_s = dur_h * 3600.0;

    const clock_t mid_time = clock();
    const double tergen_time = (double)(mid_time - start_time) / CLOCKS_PER_SEC;

    printf("\nStarting simulation\n");
    run_trial(&trl);

    if (timing_enabled) {
        const clock_t end_time = clock();
        const double simul_time = (double)(end_time - mid_time) / CLOCKS_PER_SEC;
        const double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        printf("\nTerrain generation took %g sec\n", tergen_time);
        printf("Simulation took %g sec\n", simul_time);
        printf("Total time %g sec\n", elapsed_time);
    }

    terrain_terminate(tp);
    terrain_destroy(tp);

    return 0;
}

/**************** Only I/O utility functions below this point ****************/

void progress_bar_update(const unsigned int current,
                         const unsigned int total,
                         const time_t start_time)
{
    const unsigned int bar_width = 40;
    const float progress = (float)current / (float)total;
    const unsigned int pos = (unsigned int)((float)bar_width * progress);

    const time_t now = time(NULL);
    const double elapsed = difftime(now, start_time);

    printf("\r[");
    for (unsigned int i = 0; i < bar_width; ++i) {
        if (i < pos) {
            printf("#");
        }
        else if (i == pos) {
            printf("=");
        }
        else {
            printf("-");
        }
    }

    /* Wait until 1 % progress to get a time estimate */
    printf("] %3d%% | ETA: ", (int)(progress * 100.0f));
    if (progress > 0.01f) {
        const int eta_sec = (int)(elapsed / progress - elapsed);
        printf("%02d:%02d", eta_sec / 60, eta_sec % 60);
    }
    else {
        printf("--:--");
    }

    fflush(stdout);
}

/* Internal time unit seconds, print in HHH:MM:SS.sss format */
#define FMTBUFLEN 20

const char *hhhmmss_formatter(const double t)
{
    static CMB_THREAD_LOCAL char fmtbuf[FMTBUFLEN];

    double tmp = t;
    const unsigned h = (unsigned)(tmp / 3600.0);
    tmp -= (double)(h * 3600);
    const unsigned m = (unsigned)(tmp / 60.0);
    tmp -= (double)(m * 60.0);
    const double s = tmp;
    (void)snprintf(fmtbuf, FMTBUFLEN, "%03d:%02d:%04.1f", h, m, s);

    return fmtbuf;
}

struct vtkhdf_handle {
    hid_t file_id;
    int num_targets;
    unsigned long current_step;

    hid_t pts_dset;
    hid_t status_dset;
    hid_t rcs_dset;
    hid_t is_awacs_dset;
    hid_t awacs_dir_dset;
    hid_t sensor_dir_dset;

    hid_t step_vals_dset;
    hid_t step_pts_off_dset;
    hid_t step_cell_off_dset;
    hid_t step_part_off_dset;
    hid_t step_num_parts_dset;
    hid_t step_conn_id_off_dset;
};

struct vtkhdf_handle *vtkhdf_create(void)
{
    struct vtkhdf_handle *h = cmi_malloc(sizeof(struct vtkhdf_handle));

    return h;
}

void vtkhdf_destroy(struct vtkhdf_handle *h)
{
    cmi_free(h);
}

#define H5_CHECK(x) do { \
        hid_t __res = (x); \
        if (__res < 0) { \
            fprintf(stderr, "HDF5 Error at %s:%d (Function: %s)\n", __FILE__, __LINE__, #x); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

void write_attr_string(const hid_t loc_id, const char *const name, const char *const value)
{
    const hid_t type = H5Tcopy(H5T_C_S1);
    H5_CHECK(type);
    H5_CHECK(H5Tset_size(type, strlen(value)));
    H5_CHECK(H5Tset_strpad(type, H5T_STR_NULLPAD));

    const hid_t space = H5Screate(H5S_SCALAR);
    H5_CHECK(space);
    const hid_t attr = H5Acreate2(loc_id, name, type, space, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(attr);

    H5_CHECK(H5Awrite(attr, type, value));

    H5_CHECK(H5Aclose(attr));
    H5_CHECK(H5Sclose(space));
    H5_CHECK(H5Tclose(type));
}

void write_attr_int32_array(const hid_t loc_id, const char *const name, const int32_t *const values, const hsize_t count)
{
    const hid_t type = H5T_NATIVE_INT32;
    H5_CHECK(type);
    const hid_t space = H5Screate_simple(1, &count, NULL);
    H5_CHECK(space);
    const hid_t attr = H5Acreate2(loc_id, name, type, space, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(attr);

    H5_CHECK(H5Awrite(attr, type, values));

    H5_CHECK(H5Aclose(attr));
    H5_CHECK(H5Sclose(space));
}

void write_attr_int64_array(hid_t loc_id, const char *name,
                            const int64_t *values, hsize_t count)
{
    const hid_t space = H5Screate_simple(1, &count, NULL);
    H5_CHECK(space);
    const hid_t attr = H5Acreate2(loc_id, name, H5T_NATIVE_INT64,
                                  space, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(attr);
    H5_CHECK(H5Awrite(attr, H5T_NATIVE_INT64, values));
    H5_CHECK(H5Aclose(attr));
    H5_CHECK(H5Sclose(space));
}

void write_attr_double_array(const hid_t loc_id, const char *const name, const double *const values, const hsize_t count)
{
    const hid_t type = H5T_NATIVE_DOUBLE;
    H5_CHECK(type);
    const hid_t space = H5Screate_simple(1, &count, NULL);
    H5_CHECK(space);
    const hid_t attr = H5Acreate2(loc_id, name, type, space, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(attr);

    H5_CHECK(H5Awrite(attr, type, values));

    H5_CHECK(H5Aclose(attr));
    H5_CHECK(H5Sclose(space));
}

void terrain_vtkhdf_write(const char *const h5_filename,
                          const float *const map,
                          const uint32_t cols,
                          const uint32_t rows,
                          const float x_scale,
                          const float y_scale)
{
    const uint32_t vis_cols = cols / stride_step;
    const uint32_t vis_rows = rows / stride_step;

    const double vis_x_scale = (double)x_scale * stride_step;
    const double vis_y_scale = (double)y_scale * stride_step;

    const hid_t fapl = H5Pcreate(H5P_FILE_ACCESS);
    H5_CHECK(fapl);
    H5_CHECK(H5Pset_libver_bounds(fapl, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST));
    const hid_t file_id = H5Fcreate(h5_filename, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
    H5_CHECK(file_id);
    H5Pclose(fapl);

    const hid_t root = H5Gcreate2(file_id, "VTKHDF", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(root);
    write_attr_string(root, "Type", "ImageData");
    const int64_t version[2] = { 2, 0 };
    write_attr_int64_array(root, "Version", version, 2);

    const int64_t extent[6] = {
        0, (int64_t)vis_cols - 1,
        0, (int64_t)vis_rows - 1,
        0, 0
    };

    write_attr_int64_array(root, "WholeExtent", extent, 6);

    const double total_x = (cols - 1) * (double)x_scale;
    const double total_y = (rows - 1) * (double)y_scale;
    const double origin[3] = { -(total_x / 2.0), -(total_y / 2.0), 0.0 };
    const double spacing[3] = { vis_x_scale, vis_y_scale, 1.0 };
    const double direction[9] = { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };

    write_attr_double_array(root, "Origin", origin, 3);
    write_attr_double_array(root, "Spacing", spacing, 3);
    write_attr_double_array(root, "Direction", direction, 9);

    const hid_t pd_group = H5Gcreate2(root, "PointData", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(pd_group);
    write_attr_string(pd_group, "Scalars", "Elevation");

    float *vis_map = malloc((size_t)vis_cols * (size_t)vis_rows * sizeof(float));
    for (uint32_t r = 0; r < vis_rows; r++) {
        for (uint32_t c = 0; c < vis_cols; c++) {
            const uint32_t src_idx = (r * stride_step * cols) + (c * stride_step);
            vis_map[r * vis_cols + c] = map[src_idx];
        }
    }

    const hsize_t map_dims[3] = { (hsize_t)vis_rows, (hsize_t)vis_cols, 1 };
    const hid_t map_space = H5Screate_simple(3, map_dims, NULL);
    H5_CHECK(map_space);
    const hid_t map_dset = H5Dcreate2(pd_group, "Elevation", H5T_NATIVE_FLOAT,
                                      map_space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(map_dset);
    H5_CHECK(H5Dwrite(map_dset, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, vis_map));

    H5_CHECK(H5Dclose(map_dset));
    H5_CHECK(H5Sclose(map_space));
    H5_CHECK(H5Gclose(pd_group));
    H5_CHECK(H5Gclose(root));
    H5_CHECK(H5Fclose(file_id));

    free(vis_map);
}

/* Helper to create mandatory dummy groups for ParaView's strict reader */
static void write_dummy_topology_stepped(hid_t root, const char *name,
                                         unsigned long nsteps)
{
    const hid_t group = H5Gcreate2(root, name, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    int64_t *zeros = calloc(nsteps, sizeof(int64_t));

    const hsize_t dims[1]  = { nsteps };
    const hsize_t maxd[1]  = { H5S_UNLIMITED };
    const hsize_t chunk[1] = { 1 };

    const hid_t dcpl = H5Pcreate(H5P_DATASET_CREATE);
    H5Pset_chunk(dcpl, 1, chunk);

    const hid_t space = H5Screate_simple(1, dims, maxd);

    hid_t d = H5Dcreate2(group, "NumberOfCells",
                          H5T_NATIVE_INT64, space, H5P_DEFAULT, dcpl, H5P_DEFAULT);
    H5Dwrite(d, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, zeros);
    H5Dclose(d);

    d = H5Dcreate2(group, "NumberOfConnectivityIds",
                   H5T_NATIVE_INT64, space, H5P_DEFAULT, dcpl, H5P_DEFAULT);
    H5Dwrite(d, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, zeros);
    H5Dclose(d);

    d = H5Dcreate2(group, "Offsets",
                   H5T_NATIVE_INT64, space, H5P_DEFAULT, dcpl, H5P_DEFAULT);
    H5Dwrite(d, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, zeros);
    H5Dclose(d);

    const hsize_t one = 1;
    const hid_t conn_space = H5Screate_simple(1, &one, NULL);
    d = H5Dcreate2(group, "Connectivity",
                   H5T_NATIVE_INT64, conn_space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(d, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, zeros);
    H5Dclose(d);
    H5Sclose(conn_space);

    H5Sclose(space);
    H5Pclose(dcpl);
    H5Gclose(group);

    free(zeros);
}

void events_vtkhdf_init(struct vtkhdf_handle *h, const char *filename, const int n_targets)
{
    h->num_targets = n_targets;
    h->current_step = 0;

    const int total_pts = n_targets + 1;

    h->file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (h->file_id < 0) {
        fprintf(stderr, "Failed to create HDF5 file: %s\n", filename);
        exit(EXIT_FAILURE);
    }

    const hid_t root = H5Gcreate2(h->file_id, "VTKHDF", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(root);
    write_attr_string(root, "Type", "PolyData");

    const int32_t version[2] = {2, 0};
    const hsize_t v_dim = 2;
    const hid_t v_space = H5Screate_simple(1, &v_dim, NULL);
    H5_CHECK(v_space);
    const hid_t v_attr = H5Acreate2(root, "Version", H5T_NATIVE_INT32, v_space, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(v_attr);
    H5_CHECK(H5Awrite(v_attr, H5T_NATIVE_INT32, version));
    H5_CHECK(H5Aclose(v_attr));
    H5_CHECK(H5Sclose(v_space));

    /* --- Static topology metadata --- */
    const hsize_t one_dim = 1;
    const hid_t static_space = H5Screate_simple(1, &one_dim, NULL);
    H5_CHECK(static_space);
    const int64_t static_count = (int64_t)total_pts;

    const hid_t d_np = H5Dcreate2(root, "NumberOfPoints", H5T_NATIVE_INT64, static_space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(d_np);
    H5_CHECK(H5Dwrite(d_np, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, &static_count));
    H5_CHECK(H5Dclose(d_np));

    const hid_t d_nc = H5Dcreate2(root, "NumberOfCells", H5T_NATIVE_INT64, static_space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(d_nc);
    H5_CHECK(H5Dwrite(d_nc, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, &static_count));
    H5_CHECK(H5Dclose(d_nc));

    const hid_t d_nci = H5Dcreate2(root, "NumberOfConnectivityIds", H5T_NATIVE_INT64, static_space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(d_nci);
    H5_CHECK(H5Dwrite(d_nci, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, &static_count));
    H5_CHECK(H5Dclose(d_nci));

    /* --- Primary point data --- */
    const hsize_t p_dims[2] = {0, 3};
    const hsize_t p_max[2] = {H5S_UNLIMITED, 3};
    const hsize_t p_chunk[2] = {(hsize_t)total_pts, 3};
    const hid_t p_dcpl = H5Pcreate(H5P_DATASET_CREATE);
    H5_CHECK(p_dcpl);
    H5_CHECK(H5Pset_chunk(p_dcpl, 2, p_chunk));
    const hid_t p_space = H5Screate_simple(2, p_dims, p_max);
    H5_CHECK(p_space);
    H5_CHECK(h->pts_dset = H5Dcreate2(root, "Points", H5T_NATIVE_FLOAT, p_space, H5P_DEFAULT, p_dcpl, H5P_DEFAULT));
    const hid_t pd_group = H5Gcreate2(root, "PointData", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(pd_group);
    write_attr_string(pd_group, "Scalars", "Detection_Status");
    write_attr_string(pd_group, "Vectors", "Sensor_Direction");

    const hsize_t s_dims[1] = {0};
    const hsize_t s_max[1] = {H5S_UNLIMITED};
    const hsize_t s_chunk[1] = {(hsize_t)total_pts};
    const hid_t s_dcpl = H5Pcreate(H5P_DATASET_CREATE);
    H5_CHECK(s_dcpl);
    H5_CHECK(H5Pset_chunk(s_dcpl, 1, s_chunk));
    const hid_t s_space = H5Screate_simple(1, s_dims, s_max);
    H5_CHECK(s_space);

    H5_CHECK(h->status_dset = H5Dcreate2(pd_group, "Detection_Status", H5T_NATIVE_FLOAT, s_space, H5P_DEFAULT, s_dcpl, H5P_DEFAULT));
    H5_CHECK(h->is_awacs_dset = H5Dcreate2(pd_group, "Is_AWACS", H5T_NATIVE_FLOAT, s_space, H5P_DEFAULT, s_dcpl, H5P_DEFAULT));
    H5_CHECK(h->rcs_dset = H5Dcreate2(pd_group, "RCS", H5T_NATIVE_FLOAT, s_space, H5P_DEFAULT, s_dcpl, H5P_DEFAULT));
    H5_CHECK(h->awacs_dir_dset = H5Dcreate2(pd_group, "AWACS_Direction", H5T_NATIVE_FLOAT, p_space, H5P_DEFAULT, p_dcpl, H5P_DEFAULT));
    H5_CHECK(h->sensor_dir_dset = H5Dcreate2(pd_group, "Sensor_Direction", H5T_NATIVE_FLOAT, p_space, H5P_DEFAULT, p_dcpl, H5P_DEFAULT));

    H5_CHECK(H5Pclose(p_dcpl));
    H5_CHECK(H5Sclose(p_space));
    H5_CHECK(H5Pclose(s_dcpl));
    H5_CHECK(H5Sclose(s_space));

    /* --- Topology --- */
    const hid_t v_group = H5Gcreate2(root, "Vertices", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(v_group);
    const int64_t n_cells = (int64_t)total_pts;
    const int64_t n_conn = (int64_t)total_pts;
    int64_t *conn = malloc(total_pts * sizeof(int64_t));
    int64_t *off = malloc((total_pts + 1) * sizeof(int64_t));
    for (int i = 0; i < total_pts; i++) {
        conn[i] = i;
        off[i] = i;
    }
    off[total_pts] = total_pts;

    const hsize_t c_dim = (hsize_t)total_pts;
    const hid_t c_space = H5Screate_simple(1, &c_dim, NULL);
    H5_CHECK(c_space);

    const hsize_t o_dim = (hsize_t)total_pts + 1;
    const hid_t o_space = H5Screate_simple(1, &o_dim, NULL);
    H5_CHECK(o_space);

    const hid_t c_dset = H5Dcreate2(v_group, "Connectivity", H5T_NATIVE_INT64, c_space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(c_dset);
    H5_CHECK(H5Dwrite(c_dset, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, conn));
    H5_CHECK(H5Dclose(c_dset));

    const hid_t o_dset = H5Dcreate2(v_group, "Offsets", H5T_NATIVE_INT64, o_space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(o_dset);
    H5_CHECK(H5Dwrite(o_dset, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, off));
    H5_CHECK(H5Dclose(o_dset));

    const hid_t d_v_nc = H5Dcreate2(v_group, "NumberOfCells", H5T_NATIVE_INT64, static_space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(d_v_nc);
    H5_CHECK(H5Dwrite(d_v_nc, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, &n_cells));
    H5_CHECK(H5Dclose(d_v_nc));

    const hid_t d_v_nci = H5Dcreate2(v_group, "NumberOfConnectivityIds", H5T_NATIVE_INT64, static_space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(d_v_nci);
    H5_CHECK(H5Dwrite(d_v_nci, H5T_NATIVE_INT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, &n_conn));
    H5_CHECK(H5Dclose(d_v_nci));

    free(conn);
    free(off);

    H5_CHECK(H5Sclose(o_space));
    H5_CHECK(H5Sclose(c_space));
    H5_CHECK(H5Sclose(static_space));
    H5_CHECK(H5Gclose(v_group));

    /* --- Temporal metadata --- */
    const hid_t st_group = H5Gcreate2(root, "Steps", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(st_group);
    const hsize_t t_dims[1] = {0};
    const hsize_t t_max[1] = {H5S_UNLIMITED};
    const hsize_t t_chunk[1] = {1};
    const hid_t t_dcpl = H5Pcreate(H5P_DATASET_CREATE);
    H5_CHECK(t_dcpl);
    H5_CHECK(H5Pset_chunk(t_dcpl, 1, t_chunk));
    const hid_t t_space = H5Screate_simple(1, t_dims, t_max);
    H5_CHECK(t_space);

    H5_CHECK(h->step_vals_dset = H5Dcreate2(st_group, "Values", H5T_NATIVE_FLOAT, t_space, H5P_DEFAULT, t_dcpl, H5P_DEFAULT));
    H5_CHECK(h->step_pts_off_dset = H5Dcreate2(st_group, "PointOffsets", H5T_NATIVE_INT64, t_space, H5P_DEFAULT, t_dcpl, H5P_DEFAULT));
    H5_CHECK(h->step_part_off_dset = H5Dcreate2(st_group, "PartOffsets", H5T_NATIVE_INT64, t_space, H5P_DEFAULT, t_dcpl, H5P_DEFAULT));
    H5_CHECK(h->step_num_parts_dset = H5Dcreate2(st_group, "NumberOfParts", H5T_NATIVE_INT64, t_space, H5P_DEFAULT, t_dcpl, H5P_DEFAULT));

    const hsize_t t2_dims[2]  = { 0, 5 };
    const hsize_t t2_max[2]   = { H5S_UNLIMITED, 5 };
    const hsize_t t2_chunk[2] = { 1, 5 };
    const hid_t t2_dcpl = H5Pcreate(H5P_DATASET_CREATE);
    H5_CHECK(t2_dcpl);
    H5_CHECK(H5Pset_chunk(t2_dcpl, 2, t2_chunk));
    const hid_t t2_space = H5Screate_simple(2, t2_dims, t2_max);
    H5_CHECK(t2_space);

    H5_CHECK(h->step_cell_off_dset    = H5Dcreate2(st_group, "CellOffsets",
                 H5T_NATIVE_INT64, t2_space, H5P_DEFAULT, t2_dcpl, H5P_DEFAULT));
    H5_CHECK(h->step_conn_id_off_dset = H5Dcreate2(st_group, "ConnectivityIdOffsets",
                 H5T_NATIVE_INT64, t2_space, H5P_DEFAULT, t2_dcpl, H5P_DEFAULT));

    H5_CHECK(H5Pclose(t2_dcpl));
    H5_CHECK(H5Sclose(t2_space));

    H5_CHECK(H5Pclose(t_dcpl));
    H5_CHECK(H5Sclose(t_space));
    H5_CHECK(H5Gclose(st_group));
    H5_CHECK(H5Gclose(pd_group));
    H5_CHECK(H5Gclose(root));
    H5_CHECK(H5Fflush(h->file_id, H5F_SCOPE_GLOBAL));
}

void events_vtkhdf_append(struct vtkhdf_handle *h, const struct target *targets, const struct platform_state *awacs, const float sensor_dir, const float time_val)
{
    const int n = h->num_targets;
    const int total_pts = n + 1;
    const hsize_t n_steps = h->current_step + 1;

    /* --- Extend datasets --- */
    const hsize_t p_ext[2] = { n_steps * total_pts, 3 };
    H5_CHECK(H5Dset_extent(h->pts_dset, p_ext));
    H5_CHECK(H5Dset_extent(h->awacs_dir_dset, p_ext));
    H5_CHECK(H5Dset_extent(h->sensor_dir_dset, p_ext));

    const hsize_t s_ext[1] = { n_steps * total_pts };
    H5_CHECK(H5Dset_extent(h->status_dset, s_ext));
    H5_CHECK(H5Dset_extent(h->rcs_dset, s_ext));
    H5_CHECK(H5Dset_extent(h->is_awacs_dset, s_ext));

    const hsize_t t_ext[1] = { n_steps };
    H5_CHECK(H5Dset_extent(h->step_vals_dset, t_ext));
    H5_CHECK(H5Dset_extent(h->step_pts_off_dset, t_ext));
    H5_CHECK(H5Dset_extent(h->step_part_off_dset, t_ext));
    H5_CHECK(H5Dset_extent(h->step_num_parts_dset, t_ext));

    const hsize_t t2_ext[2] = { n_steps, 5 };
    H5_CHECK(H5Dset_extent(h->step_cell_off_dset,    t2_ext));
    H5_CHECK(H5Dset_extent(h->step_conn_id_off_dset, t2_ext));

    /* --- Fill buffers --- */
    static float p_buf[(NUM_TARGETS + 1) * 3];
    static float s_buf[NUM_TARGETS + 1];
    static float is_awacs_buf[NUM_TARGETS + 1];
    static float rcs_buf[NUM_TARGETS + 1];
    static float ad_buf[(NUM_TARGETS + 1) * 3];
    static float sd_buf[(NUM_TARGETS + 1) * 3];

    for (int i = 0; i < n; i++) {
        p_buf[i * 3 + 0] = targets[i].x_m;
        p_buf[i * 3 + 1] = targets[i].y_m;
        p_buf[i * 3 + 2] = targets[i].alt_m;
        s_buf[i] = (float)targets[i].tds;
        rcs_buf[i] = targets[i].rcs_now_m2;
        is_awacs_buf[i] = 0.0f;
        ad_buf[i * 3 + 0] = 0.0f; ad_buf[i * 3 + 1] = 0.0f; ad_buf[i * 3 + 2] = 0.0f;
        sd_buf[i * 3 + 0] = 0.0f; sd_buf[i * 3 + 1] = 0.0f; sd_buf[i * 3 + 2] = 0.0f;
    }

    p_buf[n * 3 + 0] = awacs->x;
    p_buf[n * 3 + 1] = awacs->y;
    p_buf[n * 3 + 2] = awacs->alt;
    s_buf[n] = 0.0f;
    rcs_buf[n] = 0.0f;   /* AWACS platform has no RCS in this context */
    is_awacs_buf[n] = 1.0f;
    ad_buf[n * 3 + 0] = cosf(awacs->dir);
    ad_buf[n * 3 + 1] = sinf(awacs->dir);
    ad_buf[n * 3 + 2] = 0.0f;

    const float rel_angle = sensor_dir - awacs->dir;
    const float fwd  = cosf(rel_angle);
    const float left = sinf(rel_angle) * cosf(awacs->rol);

    sd_buf[n * 3 + 0] = fwd * cosf(awacs->dir) - left * sinf(awacs->dir);
    sd_buf[n * 3 + 1] = fwd * sinf(awacs->dir) + left * cosf(awacs->dir);
    sd_buf[n * 3 + 2] = sinf(rel_angle) * sinf(awacs->rol);

    /* --- Write data hyperslabs --- */
    const hsize_t start_2d[2] = { h->current_step * total_pts, 0 };
    const hsize_t count_2d[2] = { (hsize_t)total_pts, 3 };
    const hid_t mspace_2d = H5Screate_simple(2, count_2d, NULL);

    hid_t fspace = H5Dget_space(h->pts_dset);
    H5_CHECK(fspace);
    H5_CHECK(H5Sselect_hyperslab(fspace, H5S_SELECT_SET, start_2d, NULL, count_2d, NULL));
    H5_CHECK(H5Dwrite(h->pts_dset, H5T_NATIVE_FLOAT, mspace_2d, fspace, H5P_DEFAULT, p_buf));
    H5_CHECK(H5Sclose(fspace));

    H5_CHECK(fspace = H5Dget_space(h->awacs_dir_dset));
    H5_CHECK(H5Sselect_hyperslab(fspace, H5S_SELECT_SET, start_2d, NULL, count_2d, NULL));
    H5_CHECK(H5Dwrite(h->awacs_dir_dset, H5T_NATIVE_FLOAT, mspace_2d, fspace, H5P_DEFAULT, ad_buf));
    H5_CHECK(H5Sclose(fspace));

    H5_CHECK(fspace = H5Dget_space(h->sensor_dir_dset));
    H5_CHECK(H5Sselect_hyperslab(fspace, H5S_SELECT_SET, start_2d, NULL, count_2d, NULL));
    H5_CHECK(H5Dwrite(h->sensor_dir_dset, H5T_NATIVE_FLOAT, mspace_2d, fspace, H5P_DEFAULT, sd_buf));
    H5_CHECK(H5Sclose(fspace));
    H5_CHECK(H5Sclose(mspace_2d));

    const hsize_t start_1d[1] = { h->current_step * total_pts };
    const hsize_t count_1d[1] = { (hsize_t)total_pts };
    const hid_t mspace_1d = H5Screate_simple(1, count_1d, NULL);

    H5_CHECK(fspace = H5Dget_space(h->status_dset));
    H5_CHECK(H5Sselect_hyperslab(fspace, H5S_SELECT_SET, start_1d, NULL, count_1d, NULL));
    H5_CHECK(H5Dwrite(h->status_dset, H5T_NATIVE_FLOAT, mspace_1d, fspace, H5P_DEFAULT, s_buf));
    H5_CHECK(H5Sclose(fspace));

    H5_CHECK(fspace = H5Dget_space(h->rcs_dset));
    H5_CHECK(H5Sselect_hyperslab(fspace, H5S_SELECT_SET, start_1d, NULL, count_1d, NULL));
    H5_CHECK(H5Dwrite(h->rcs_dset, H5T_NATIVE_FLOAT, mspace_1d, fspace, H5P_DEFAULT, rcs_buf));
    H5_CHECK(H5Sclose(fspace));

    H5_CHECK(fspace = H5Dget_space(h->is_awacs_dset));
    H5_CHECK(H5Sselect_hyperslab(fspace, H5S_SELECT_SET, start_1d, NULL, count_1d, NULL));
    H5_CHECK(H5Dwrite(h->is_awacs_dset, H5T_NATIVE_FLOAT, mspace_1d, fspace, H5P_DEFAULT, is_awacs_buf));
    H5_CHECK(H5Sclose(fspace));
    H5_CHECK(H5Sclose(mspace_1d));

    /* --- Write temporal metadata --- */
    const hsize_t t_start[1] = { h->current_step };
    const hsize_t t_count[1] = { 1 };

    const int64_t pts_off = (int64_t)(h->current_step * total_pts);
    const int64_t part_off = 0;
    const int64_t num_parts = 1;

    const hid_t t_mspace = H5Screate_simple(1, t_count, NULL);
    H5_CHECK(t_mspace);

    #define WRITE_STEP_DATA(dset, val_ptr, type) do { \
                fspace = H5Dget_space(dset); \
                H5Sselect_hyperslab(fspace, H5S_SELECT_SET, t_start, NULL, t_count, NULL); \
                H5Dwrite(dset, type, t_mspace, fspace, H5P_DEFAULT, val_ptr); \
                H5Sclose(fspace); \
        } while(0)

    WRITE_STEP_DATA(h->step_vals_dset, &time_val, H5T_NATIVE_FLOAT);
    WRITE_STEP_DATA(h->step_pts_off_dset, &pts_off, H5T_NATIVE_INT64);
    WRITE_STEP_DATA(h->step_part_off_dset, &part_off, H5T_NATIVE_INT64);
    WRITE_STEP_DATA(h->step_num_parts_dset, &num_parts, H5T_NATIVE_INT64);

    #undef WRITE_STEP_DATA
    H5_CHECK(H5Sclose(t_mspace));

    const int64_t cell_off_row[5]    = { 0, 0, 0, 0, 0 };
    const int64_t conn_id_off_row[5] = { 0, 0, 0, 0, 0 };
    const hsize_t t2_start[2] = { h->current_step, 0 };
    const hsize_t t2_count[2] = { 1, 5 };
    const hid_t t2_mspace = H5Screate_simple(2, t2_count, NULL);
    H5_CHECK(t2_mspace);

    #define WRITE_STEP_DATA_2D(dset, val_ptr) do { \
            fspace = H5Dget_space(dset); \
            H5_CHECK(fspace); \
            H5_CHECK(H5Sselect_hyperslab(fspace, H5S_SELECT_SET, t2_start, NULL, t2_count, NULL)); \
            H5_CHECK(H5Dwrite(dset, H5T_NATIVE_INT64, t2_mspace, fspace, H5P_DEFAULT, val_ptr)); \
            H5_CHECK(H5Sclose(fspace)); \
        } while(0)

    WRITE_STEP_DATA_2D(h->step_cell_off_dset,    cell_off_row);
    WRITE_STEP_DATA_2D(h->step_conn_id_off_dset, conn_id_off_row);

    #undef WRITE_STEP_DATA_2D
    H5_CHECK(H5Sclose(t2_mspace));

    h->current_step++;
}

void events_vtkhdf_close(const struct vtkhdf_handle *const h)
{
    const hid_t st_group = H5Gopen2(h->file_id, "VTKHDF/Steps", H5P_DEFAULT);
    H5_CHECK(st_group);

    const hid_t s_space = H5Screate(H5S_SCALAR);
    H5_CHECK(s_space);
    const hid_t attr = H5Acreate2(st_group, "NSteps", H5T_NATIVE_UINT64, s_space, H5P_DEFAULT, H5P_DEFAULT);
    H5_CHECK(attr);
    H5_CHECK(H5Awrite(attr, H5T_NATIVE_UINT64, &h->current_step));

    H5_CHECK(H5Aclose(attr));
    H5_CHECK(H5Sclose(s_space));
    H5_CHECK(H5Gclose(st_group));

    const hid_t root = H5Gopen2(h->file_id, "VTKHDF", H5P_DEFAULT);
    H5_CHECK(root);
    write_dummy_topology_stepped(root, "Lines",    h->current_step);
    write_dummy_topology_stepped(root, "Polygons", h->current_step);
    write_dummy_topology_stepped(root, "Strips",   h->current_step);
    H5_CHECK(H5Gclose(root));

    H5_CHECK(H5Dclose(h->pts_dset));
    H5_CHECK(H5Dclose(h->status_dset));
    H5_CHECK(H5Dclose(h->rcs_dset));
    H5_CHECK(H5Dclose(h->is_awacs_dset));
    H5_CHECK(H5Dclose(h->awacs_dir_dset));
    H5_CHECK(H5Dclose(h->sensor_dir_dset));
    H5_CHECK(H5Dclose(h->step_vals_dset));
    H5_CHECK(H5Dclose(h->step_pts_off_dset));
    H5_CHECK(H5Dclose(h->step_cell_off_dset));
    H5_CHECK(H5Dclose(h->step_part_off_dset));
    H5_CHECK(H5Dclose(h->step_num_parts_dset));
    H5_CHECK(H5Dclose(h->step_conn_id_off_dset));

    H5_CHECK(H5Fclose(h->file_id));
}

/* Export the racetrack orbit as a 3D line loop for ParaView (.vtp) */
void racetrack_write_vtp(const struct racetrack *rt, const char *filename)
{
    const unsigned int npoints = 500u;

    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for writing.\n", filename);
        return;
    }

    fprintf(fp, "<?xml version=\"1.0\"?>\n");
    fprintf(fp, "<VTKFile type=\"PolyData\" version=\"0.1\" byte_order=\"LittleEndian\">\n");
    fprintf(fp, "  <PolyData>\n");
    fprintf(fp, "    <Piece NumberOfPoints=\"%d\" NumberOfLines=\"1\">\n", npoints);
    fprintf(fp, "      <Points>\n");
    fprintf(fp, "        <DataArray type=\"Float32\" NumberOfComponents=\"3\" format=\"ascii\">\n");

    struct platform_state st;
    const float tstep = rt->orbit_time_s / (float)npoints;

    for (unsigned int i = 0; i < npoints; i++) {
        const float t = rt->start_time + ((float)i * tstep);
        /* Abusing the platform state calculator to get point coordinates */
        platform_state_update(&st, rt, t);
        fprintf(fp, "          %.2f %.2f %.2f\n", st.x, st.y, st.alt);
    }

    fprintf(fp, "        </DataArray>\n");
    fprintf(fp, "      </Points>\n");
    fprintf(fp, "      <Lines>\n");
    fprintf(fp, "        <DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n");
    fprintf(fp, "          ");
    for (unsigned int i = 0; i < npoints; i++) {
        fprintf(fp, "%d ", i);
    }

    fprintf(fp, "0\n");
    fprintf(fp, "        </DataArray>\n");
    fprintf(fp, "        <DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n");
    fprintf(fp, "          %d\n", npoints + 1);
    fprintf(fp, "        </DataArray>\n");
    fprintf(fp, "      </Lines>\n");
    fprintf(fp, "      <PointData>\n");
    fprintf(fp, "        <DataArray type=\"Float32\" Name=\"Roll_Angle\" format=\"ascii\">\n");
    for (unsigned int i = 0; i < npoints; i++) {
        platform_state_update(&st, rt, rt->start_time + (float)i * tstep);
        fprintf(fp, "          %.4f\n", st.rol * (180.0 / M_PI));
    }

    fprintf(fp, "        </DataArray>\n");
    fprintf(fp, "      </PointData>\n");
    fprintf(fp, "    </Piece>\n");
    fprintf(fp, "  </PolyData>\n");
    fprintf(fp, "</VTKFile>\n");

    fclose(fp);
}
