/*
* tut_5_3.h - Shared model definitions for CPU and CUDA translation units.
 *
 * This header is included by both tut_5_3.c (gcc, C23) and tut_5_3.cu
 * (nvcc, C++17 host). Keep it compatible with both: no C23-only syntax,
 * no CUDA-only syntax. Pure data layout and constants only.
 *
 * Copyright (c) Asbjørn M. Bonvik 2026.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef CIMBA_TUT_5_3_H
#define CIMBA_TUT_5_3_H

#include <cuda_runtime.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CUDA_CHECK(call)                                                       \
    do {                                                                       \
        cudaError_t _e = (call);                                               \
        if (_e != cudaSuccess) {                                               \
            fprintf(stderr, "CUDA error at %s:%d — %s\n",                      \
                    __FILE__, __LINE__, cudaGetErrorString(_e));               \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (0)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Noise and terrain generation parameters — must match tut_5_3.c exactly.
 * Declared as static const rather than #define so they have a type and are
 * visible to the CUDA kernel without preprocessor fragility.
 */
static const float terrain_max        = 2500.0f;
static const unsigned int terrain_octaves   = 6u;
static const float terrain_initfreq   = 1.0f / 100000.0f;
static const float terrain_ridginess  = 1.2f;
static const float terrain_peakiness  = 1.7f;
static const float terrain_stddev     = 3.0f;
static const float atm_N0_units    = 313.0f;    /* sea-level refractivity, N-units */
static const float atm_scale_h_m   = 7000.0f;   /* refractivity scale height, m */

/*
 * Per-dwell radar timing. The host-side sensor process runs at 1 Hz; the
 * GPU kernel internally simulates n_dwells_per_step successive antenna
 * positions per host call, each separated by dwell_time_s of simulated
 * time. */
static const float        dwell_time_s        = 0.04f;
static const unsigned int n_dwells_per_step   = 25u;   /* 25 * 0.04 = 1.0 s */

/* For the decimated VTK output */
static const unsigned int stride_step = 32u;
extern const char *terrain_h5name;

/*
 * Biome parameters. biome_gamma_db[i] is the constant-gamma clutter
 * reflectivity in dB for biome i, per the Barton/Morchin model
 *   sigma_0 = gamma * sin(grazing_angle)
 * Typical values: smooth surface around -25 dB, crops/grass -15 dB,
 * rough terrain -5 dB. The biome is selected by target/sample altitude;
 * the elevations array splits the terrain into NBIOMES bands. */
#define NBIOMES 3
static const float biome_gamma_db[NBIOMES]      = { -20.0f, -15.0f, -5.0f };
static const float biome_elevations[NBIOMES - 1] = { 400.0f, 1000.0f };

/* Radar wavelength. S-band, ~3 GHz. The multipath lobe spacing in target
 * altitude is λ × R / (2 × h_sensor); scale the wavelength and the lobe
 * pattern scales linearly. */
static const float radar_wavelength_m = 0.1f;

/* Per-biome specular reflection coefficient |Γ_max|, at the smooth-surface
 * limit. The Rayleigh roughness penalty applied inline reduces the
 * effective |Γ| as grazing angle and surface roughness grow. */
static const float biome_specular_max[NBIOMES] = { 0.90f, 0.50f, 0.10f };

/* Per-biome surface RMS height roughness in meters. The small value for
 * biome 0 (5 cm) represents nearly specular surfaces; biomes 1 and 2 are
 * rough enough that multipath effectively vanishes at S-band over
 * AWACS-style grazing geometries. */
static const float biome_roughness_m[NBIOMES] = { 0.05f, 0.3f, 2.0f };

/*
 * Radar parameters that affect the clutter+CFAR detection model. Made
 * a struct rather than scattered fields so adding new knobs later
 * doesn't ripple through every function signature.
 *
 * range_res_m       Range resolution (c / (2 * effective_bandwidth)).
 *                   ~150 m for a 1 MHz pulse-compression radar.
 * n_pulses_per_dwell  Number of pulses integrated per dwell. Determines
 *                   the SNR boost from non-coherent integration.
 * cfar_n_ref        Number of reference range cells per side for CA-CFAR.
 * cfar_n_guard      Number of guard cells per side (skipped, between
 *                   the test cell and the reference cells).
 * cfar_alpha        Threshold multiplier. For CA-CFAR over 2*N_ref cells
 *                   with Pfa = 1e-6, set to N * (Pfa^(-1/N) - 1).
 *                   At N = 16: alpha ≈ 5.62.
 * noise_floor_norm  Thermal noise floor in the same normalized units as
 *                   the integrated cell energy. Sets the noise-limited
 *                   sensitivity floor.
 * cell_grid_n_range, cell_grid_n_cross
 *                   Number of texture samples per resolution cell in
 *                   range and cross-range. 4x4 = 16 samples per cell.
 */
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

/*
 * The terrain model.
 * After terrain_init() returns in the CUDA version, map and d_map both point
 * to device memory. CPU code must not dereference them directly; use the
 * host-side elevation lookup only before terrain_init() hands off to the GPU.
 */
struct terrain {
    float ref_lat_r;        /* Reference point, radians latitude */
    float ref_lon_r;        /* Reference point, radians longitude */
    float x_scale;          /* Meters per arcsecond, longitude */
    float y_scale;          /* Meters per arcsecond, latitude */
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    unsigned int cols;
    unsigned int rows;
    float *map;             /* Device pointer after terrain_init() in CUDA version */
    float *d_map;           /* Explicit alias for clarity in kernel-calling code */

    void *cu_array;          /* cudaArray_t — opaque, declared as void * for C compatibility */
    unsigned long long tex;  /* cudaTextureObject_t is unsigned long long under the hood */
    int device;              /* CUDA device this terrain's cudaArray/texture lives on */

    int p[512];             /* Perlin permutation table, CPU-side only */
};

/*
 * Function declarations.
 * terrain_init and terrain_terminate are implemented in tut_5_3.cu;
 * the rest live in tut_5_3.c. Declared with C linkage so nvcc and gcc
 * agree on name mangling.
 */
struct terrain *terrain_create(void);
void terrain_initialize(struct terrain *tp,
                  float tsz_w, float tsz_h,
                  float ref_lat, float ref_lon,
                  uint64_t terrain_seed);
void terrain_terminate(struct terrain *tp);
void terrain_destroy(struct terrain *tp);

unsigned terrain_index(const struct terrain *tp, float x, float y);
float terrain_elevation(const struct terrain *tp, float x, float y);

void terrain_vtkhdf_write(const char *h5_filename,
                          const float *map,
                          unsigned cols,
                          unsigned rows,
                          float x_scale,
                          float y_scale);


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

/* Opaque from the C side. Fields declared here so the .cu file can access
 * them directly without accessors. void * is used for CUDA-only types so
 * this header stays valid C. */
struct sensor_gpu_state {
    unsigned int n_targets;
    const struct terrain *terrain;

    /* Target SoA in device memory */
    void *d_x, *d_y, *d_alt;
    void *d_dir, *d_vel, *d_time;
    void *d_height, *d_rcs;

    /* Results in device memory (persistent across steps) */
    void *d_tds;
    void *d_detected;
    void *d_mode;

    /* Ray-march work queue: indices of targets that survived triage
     * and need geometric ray-marching. d_queue_count is a single int. */
    void *d_queue;
    void *d_queue_count;
    void *d_queue_n_dwells;
    void *d_queue_first_dwell;

    /* RNG state in device memory */
    void *d_rng;

    cudaStream_t stream;

    /* Pinned host scratch buffers */
    void *h_pinned_upload;
    void *h_pinned_results;
};

void sensor_gpu_load(void);

void sensor_gpu_init(struct sensor_gpu_state *gpu,
                     unsigned int n_targets,
                     uint64_t rng_seed,
                     const struct terrain *terp,
                     cudaStream_t cstream);

void sensor_gpu_terminate(struct sensor_gpu_state *gpu);

void sensor_gpu_step(struct sensor_gpu_state *gpu,
                     const float *x, const float *y, const float *alt,
                     const float *dir, const float *vel, const float *time_s,
                     const float *height, const float *rcs, const int *mode,
                     float sx, float sy, float sa,
                     float platform_hdg, float platform_rol,
                     float base_dir, float rot_inc_per_dwell,
                     float beamwidth_r,
                     float sim_time,
                     float r_earth,
                     float elev_min, float elev_max,
                     float ref_range_m, float ref_rcs_m2,
                     const struct radar_params *radar,
                     int *tds_out, uint8_t *detected_out);

#ifdef __cplusplus
}
#endif

#endif //CIMBA_TUT_5_3_H
