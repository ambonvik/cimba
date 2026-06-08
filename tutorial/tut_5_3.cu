/*
 * tutorial/tut_5_2.cu CUDA implementation of terrain generation and sensor
 * detection pipeline: Functionally same as tut_5_1, but do not expect bit-identical
 * outputs, since the CUDA-side pseudo-random number generators are different from
 * the Cimba CPU-side generators.
 *
 * Requires compute capability 8.6 (RTX 3090, Ampere), compile with -arch=sm_86.
 *
 * Copyright (c) Asbjørn M. Bonvik 2026.
 * Licensed under the Apache License, Version 2.0.
 */

#include <cuda_runtime.h>
#include <curand_kernel.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "tut_5_3.h"

/******************************************************************************
 * CUDA GPU implementation of the Perlin noise terrain generation from tut_5_1.c.
 * Drops in as a replacement for terrain_init(); the rest of the simulation is
 * unchanged. The terrain map remains in device memory for subsequent ray-marching
 * by the detection pipeline kernel.
 */

/*
 * Permutation table in constant memory.
 * All threads in a warp read the same index simultaneously → single broadcast,
 * no bank conflicts, effectively free compared to global memory.
 * 512 ints = 2 kB, well within the 64 kB constant memory limit.
 */
__constant__ int d_perm[512];

/*
 * Device-side Perlin helpers — exact translations of the CPU originals.
 * Marked __device__ __forceinline__ to guarantee inlining into the kernel;
 * the compiler will keep everything in registers with no function call overhead.
*/

__device__ __forceinline__
float dev_fade(const float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

__device__ __forceinline__
float dev_lerp(const float t, const float a, const float b)
{
    return a + t * (b - a);
}

__device__ __forceinline__
float dev_grad(const int hash, const float x, const float y)
{
    const int h = hash & 15;
    const float u = (h < 8) ? x : y;
    const float v = (h < 4) ? y : ((h == 12 || h == 14) ? x : 0.0f);
    return (((h & 1) == 0) ? u : -u) + (((h & 2) == 0) ? v : -v);
}

__device__ __forceinline__
float dev_perlin2d(const float x, const float y)
{
    /* Integer cell coordinates, wrapped to [0, 255] */
    const int X = ((int)floorf(x)) & 255;
    const int Y = ((int)floorf(y)) & 255;

    /* Fractional offsets within the cell */
    const float xf = x - floorf(x);
    const float yf = y - floorf(y);

    /* Fade curves */
    const float u = dev_fade(xf);
    const float v = dev_fade(yf);

    /* Hash the four corners */
    const int A  = d_perm[X]     + Y;
    const int AA = d_perm[A];
    const int AB = d_perm[A + 1];
    const int B  = d_perm[X + 1] + Y;
    const int BA = d_perm[B];
    const int BB = d_perm[B + 1];

    return dev_lerp(v,
        dev_lerp(u, dev_grad(d_perm[AA], xf,        yf),
                    dev_grad(d_perm[BA], xf - 1.0f, yf)),
        dev_lerp(u, dev_grad(d_perm[AB], xf,        yf - 1.0f),
                    dev_grad(d_perm[BB], xf - 1.0f, yf - 1.0f)));
}

/*
 * Main terrain generation kernel.
 *
 * Grid layout: one thread per (col, row) cell.
 *   gridDim  = ceil(cols/32) × ceil(rows/16)   [tunable, see launch site]
 *   blockDim = 32 × 16  (512 threads, good occupancy on sm_86)
 *
 * Each thread is fully independent — no shared memory, no synchronization.
 * The octave accumulation loop runs entirely in registers.
 *
 * Parameters mirror terrain_init() exactly so the host wrapper is thin.
 */
__global__
void terrain_generate_kernel(cudaSurfaceObject_t out_surf,
                              const unsigned int cols, const unsigned int rows,
                              const float x_scale, const float y_scale,
                              const float terrain_max,
                              const unsigned int octaves,
                              const float init_freq,
                              const float ridginess,
                              const float peakiness,
                              const float noise_stddev,    /* NEW */
                              const uint64_t seed)         /* NEW */
{
    const unsigned int col = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int row = blockIdx.y * blockDim.y + threadIdx.y;
    if (col >= cols || row >= rows) return;

    const float xs = ((float)col - (float)cols * 0.5f) * x_scale;
    const float ys = ((float)row - (float)rows * 0.5f) * y_scale;

    /* ---- Octave accumulation, unchanged ---- */
    float h      = 0.0f;
    float freq   = init_freq;
    float amp    = 1.0f;
    float weight = 1.0f;
    float ampsum = 0.0f;

    for (unsigned int i = 0; i < octaves; i++) {
        float n = dev_perlin2d(xs * freq, ys * freq);
        n = powf(1.0f - fabsf(n), ridginess);
        h += n * amp * weight;
        weight = n;
        freq   *= 2.05f;
        ampsum += amp;
        amp    *= 0.5f;
    }

    h /= ampsum;
    h  = powf(h, peakiness);

    /* ---- Per-thread normal noise via Philox ----
     * Each thread initializes its own state from (seed, thread-unique offset).
     * Philox is designed for exactly this — no global state, fully parallel,
     * statistically sound. Initialization is a few dozen ns; one draw is ~5 ns. */
    curandStatePhilox4_32_10_t rng_state;
    const unsigned long long thread_idx =
        (unsigned long long)row * (unsigned long long)cols + (unsigned long long)col;
    curand_init(seed, thread_idx, 0, &rng_state);

    const float noise = curand_normal(&rng_state) * noise_stddev;

    const float h_sum = fmaxf(0.0f, h * terrain_max + noise);

    surf2Dwrite(h_sum, out_surf, col * (int)sizeof(float), row);
}

/*
 * Reduction kernel to compute min, max, sum, and sum-of-squares in one pass.
 * Uses shared memory within each block, then atomics to accumulate globally.
 *
 * This avoids pulling the entire map back to the CPU just for statistics.
 * Uses float atomicMin/Max via __float_as_int trick (pre-sm_90 devices lack
 * native float atomics; the int reinterpretation works for non-negative floats
 * because IEEE 754 positive floats are ordered identically to unsigned ints).
 */
__global__
void terrain_stats_kernel(cudaTextureObject_t tex,
                           unsigned int cols, unsigned int rows,
                           float  *g_min,  float  *g_max,
                           double *g_sum,  double *g_sum2)
{
    extern __shared__ float smem[];

    const unsigned int n      = cols * rows;
    const unsigned int tid    = threadIdx.x;
    const unsigned int gid    = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned int stride = blockDim.x * gridDim.x;

    float   thr_min  = FLT_MAX;
    float   thr_max  = -FLT_MAX;
    double  thr_sum  = 0.0;
    double  thr_sum2 = 0.0;

    /* Grid-stride loop reading from the texture */
    for (unsigned int i = gid; i < n; i += stride) {
        const unsigned int col = i % cols;
        const unsigned int row = i / cols;
        const float v = tex2D<float>(tex, col + 0.5f, row + 0.5f);
        thr_min   = fminf(thr_min, v);
        thr_max   = fmaxf(thr_max, v);
        thr_sum  += (double)v;
        thr_sum2 += (double)v * (double)v;
    }

    /* Block-level reduction for min */
    smem[tid] = thr_min;
    __syncthreads();
    for (unsigned int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) smem[tid] = fminf(smem[tid], smem[tid + s]);
        __syncthreads();
    }
    if (tid == 0) atomicMin((int *)g_min, __float_as_int(smem[0]));

    /* Block-level reduction for max */
    __syncthreads();
    smem[tid] = thr_max;
    __syncthreads();
    for (unsigned int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) smem[tid] = fmaxf(smem[tid], smem[tid + s]);
        __syncthreads();
    }
    if (tid == 0) atomicMax((int *)g_max, __float_as_int(smem[0]));

    /* Warp-level reduction for sums to reduce atomic pressure */
    __syncthreads();
    for (int mask = 16; mask > 0; mask >>= 1) {
        thr_sum  += __shfl_down_sync(0xffffffff, thr_sum,  mask);
        thr_sum2 += __shfl_down_sync(0xffffffff, thr_sum2, mask);
    }
    if ((tid & 31) == 0) {
        atomicAdd(g_sum,  thr_sum);
        atomicAdd(g_sum2, thr_sum2);
    }
}

/*
 * Host-side terrain initializer.
 * Same signature as the CPU terrain_init() so main() needs no changes,
 * except that tp->map points to a *device* allocation on return and
 * tp->d_map is set to the same pointer for clarity in calling code.
 *
 * The decimated host copy for VTK is generated with a cudaMemcpy2D stride
 * copy rather than pulling the full map, saving ~95% of PCIe bandwidth.
 */
void terrain_initialize(struct terrain *tp,
                  float tsz_w, float tsz_h,
                  float ref_lat, float ref_lon,
                  uint64_t terrain_seed)
{
    /* ---- Geometry setup (identical to CPU version) ---- */
    const double deg_to_rad     = 2.0 * M_PI / 360.0;
    const double nm_to_meters   = 1852.0;
    const double WGS84_A        = 6378137.0;
    const double WGS84_F        = 1.0 / 298.257223563;
    const double WGS84_E2       = WGS84_F * (2.0 - WGS84_F);

    tp->cols = (unsigned int)(tsz_w * nm_to_meters / 30.87);
    tp->rows = (unsigned int)(tsz_h * nm_to_meters / 30.87);
    tp->ref_lat_r = (float)(deg_to_rad * ref_lat);
    tp->ref_lon_r = (float)(deg_to_rad * ref_lon);

    const double sin_lat     = sin(tp->ref_lat_r);
    const double cos_lat     = cos(tp->ref_lat_r);
    const double common      = 1.0 - WGS84_E2 * sin_lat * sin_lat;
    const double sqrt_common = sqrt(common);
    const double N           = WGS84_A / sqrt_common;
    const double M           = WGS84_A * (1.0 - WGS84_E2) / (common * sqrt_common);
    tp->x_scale = (float)((1.0 / 3600.0) * N * cos_lat * (M_PI / 180.0));
    tp->y_scale = (float)((1.0 / 3600.0) * M *            (M_PI / 180.0));

    const float x_span = (float)(tp->cols - 1) * tp->x_scale;
    const float y_span = (float)(tp->rows - 1) * tp->y_scale;
    tp->x_min = -(x_span * 0.5f);
    tp->x_max =  (x_span * 0.5f);
    tp->y_min = -(y_span * 0.5f);
    tp->y_max =  (y_span * 0.5f);

    const size_t map_elems = (size_t)tp->cols * (size_t)tp->rows;

    /* ---- Build and upload Perlin permutation table ---- */
    int perm_host[512];
    for (int i = 0; i < 256; i++) perm_host[i] = i;

    uint64_t rng = 0x853c49e6748fea9bULL ^ terrain_seed;
    for (int i = 255; i > 0; i--) {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        const int j = (int)(rng % (uint64_t)(i + 1));
        const int tmp = perm_host[i];
        perm_host[i] = perm_host[j];
        perm_host[j] = tmp;
    }
    for (int i = 0; i < 256; i++) perm_host[256 + i] = perm_host[i];
    CUDA_CHECK(cudaMemcpyToSymbol(d_perm, perm_host, 512 * sizeof(int)));

    /* Check for CUDA capabilities and VRAM */
    int dev = -1;
    CUDA_CHECK(cudaGetDevice(&dev));   /* caller has already set the device */
    tp->device = dev;

    struct cudaDeviceProp prop;
    CUDA_CHECK(cudaGetDeviceProperties(&prop, dev));

    if (tp->cols > (unsigned)prop.maxTexture2D[0] ||
        tp->rows > (unsigned)prop.maxTexture2D[1]) {
        fprintf(stderr,
            "Terrain %ux%u exceeds device %d 2D texture limit %dx%d\n",
            tp->cols, tp->rows, dev, prop.maxTexture2D[0], prop.maxTexture2D[1]);
        exit(EXIT_FAILURE);
    }

    const size_t need = (size_t)tp->cols * (size_t)tp->rows * sizeof(float);
    size_t freeb = 0, totalb = 0;
    CUDA_CHECK(cudaMemGetInfo(&freeb, &totalb));
    if (need + need / 10u > freeb) {   /* ~10% headroom for sensor allocations */
        fprintf(stderr,
            "Terrain needs %.2f GB but only %.2f GB free on device %d (%.2f GB total)\n",
            need / 1e9, freeb / 1e9, dev, totalb / 1e9);
        exit(EXIT_FAILURE);
    }

    /* ---- Allocate the cudaArray (the only large allocation) ----
     * cudaArraySurfaceLoadStore lets the Perlin kernel write into it via a
     * surface object. The same array is read back later via a texture object. */
    cudaChannelFormatDesc channel_desc = cudaCreateChannelDesc<float>();
    cudaArray_t cu_arr = nullptr;
    CUDA_CHECK(cudaMallocArray(&cu_arr, &channel_desc, tp->cols, tp->rows,
                                cudaArraySurfaceLoadStore));

    /* ---- Build a temporary surface object for the Perlin kernel ---- */
    cudaResourceDesc surf_res = {};
    surf_res.resType = cudaResourceTypeArray;
    surf_res.res.array.array = cu_arr;
    cudaSurfaceObject_t surf_obj = 0;
    CUDA_CHECK(cudaCreateSurfaceObject(&surf_obj, &surf_res));

    /* ---- Run the Perlin kernel, writing directly into the cudaArray ---- */
    const dim3 block(32, 16);
    const dim3 grid((tp->cols + block.x - 1) / block.x,
                    (tp->rows + block.y - 1) / block.y);

    terrain_generate_kernel<<<grid, block>>>(
        surf_obj,
        tp->cols, tp->rows,
        tp->x_scale, tp->y_scale,
        terrain_max, terrain_octaves, terrain_initfreq,
        terrain_ridginess, terrain_peakiness,
        terrain_stddev, terrain_seed);

    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    /* Surface object no longer needed once generation is complete */
    CUDA_CHECK(cudaDestroySurfaceObject(surf_obj));

    /* ---- Create the long-lived texture object for the cudaArray ---- */
    cudaResourceDesc tex_res = {};
    tex_res.resType = cudaResourceTypeArray;
    tex_res.res.array.array = cu_arr;

    cudaTextureDesc tex_desc = {};
    tex_desc.addressMode[0]   = cudaAddressModeClamp;
    tex_desc.addressMode[1]   = cudaAddressModeClamp;
    tex_desc.filterMode       = cudaFilterModeLinear;
    tex_desc.readMode         = cudaReadModeElementType;
    tex_desc.normalizedCoords = 0;

    cudaTextureObject_t tex_obj = 0;
    CUDA_CHECK(cudaCreateTextureObject(&tex_obj, &tex_res, &tex_desc, nullptr));

    /* Store in struct for downstream use */
    tp->cu_array = (void *)cu_arr;
    tp->tex      = (unsigned long long)tex_obj;
    tp->map      = nullptr;
    tp->d_map    = nullptr;

    /* ---- Compute statistics by reading from the texture ---- */
    float  h_min  =  FLT_MAX;
    float  h_max  = -FLT_MAX;
    double h_sum  = 0.0;
    double h_sum2 = 0.0;

    float  *d_min  = nullptr, *d_max  = nullptr;
    double *d_sum  = nullptr, *d_sum2 = nullptr;
    CUDA_CHECK(cudaMalloc(&d_min,  sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_max,  sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_sum,  sizeof(double)));
    CUDA_CHECK(cudaMalloc(&d_sum2, sizeof(double)));

    CUDA_CHECK(cudaMemcpy(d_min,  &h_min,  sizeof(float),  cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_max,  &h_max,  sizeof(float),  cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemset(d_sum,  0,       sizeof(double)));
    CUDA_CHECK(cudaMemset(d_sum2, 0,       sizeof(double)));

    const unsigned int stat_threads = 256u;
    const unsigned int stat_blocks_needed =
        (unsigned int)((map_elems + stat_threads - 1) / stat_threads);
    const unsigned int stat_blocks = (stat_blocks_needed < 1024u)
                                      ? stat_blocks_needed : 1024u;

    terrain_stats_kernel<<<stat_blocks, stat_threads,
                            stat_threads * sizeof(float)>>>(
        tex_obj, tp->cols, tp->rows, d_min, d_max, d_sum, d_sum2);

    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaMemcpy(&h_min,  d_min,  sizeof(float),  cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(&h_max,  d_max,  sizeof(float),  cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(&h_sum,  d_sum,  sizeof(double), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(&h_sum2, d_sum2, sizeof(double), cudaMemcpyDeviceToHost));

    CUDA_CHECK(cudaFree(d_min));
    CUDA_CHECK(cudaFree(d_max));
    CUDA_CHECK(cudaFree(d_sum));
    CUDA_CHECK(cudaFree(d_sum2));

    const double count  = (double)map_elems;
    const double mean   = h_sum / count;
    const double stddev = sqrt(h_sum2 / count - mean * mean);
}

/* terrain_terminate frees device memory instead of host memory */
void terrain_terminate(struct terrain *tp)
{
    if (tp->tex != 0) {
        cudaDestroyTextureObject((cudaTextureObject_t)tp->tex);
        tp->tex = 0;
    }

    if (tp->cu_array != nullptr) {
        cudaFreeArray((cudaArray_t)tp->cu_array);
        tp->cu_array = nullptr;
    }
}

/******************************************************************************
 * CUDA detection pipeline. Replaces the per-target loop in sensor_proc.
 *
 * Design
 * ------
 * Each simulated second the host invokes sensor_gpu_step(). That uploads
 * the current target state, runs two kernels back to back, and downloads
 * the detection results.
 *
 *   1. Triage kernel (one thread per target, well-occupied block size)
 *      - Updates target position from velocity since last step.
 *      - Looks up the new terrain altitude beneath the target.
 *      - Runs the three cheap rejection tests: swept sector, radar
 *        horizon, vertical lobe / nadir hole.
 *      - For high-altitude geometries that provably clear the terrain,
 *        runs the probabilistic detection test inline.
 *      - For the remaining targets, appends the target index to a
 *        device-side work queue via a single atomicAdd.
 *
 *   2. Ray-march kernel (one warp per queued target, launched only if
 *      the queue is non-empty)
 *      - Cooperatively marches the sensor->target ray and checks for
 *        terrain shielding.
 *      - Targets that pass the ray-march run the probabilistic test;
 *        shielded targets are written as TERRAIN_SHIELDED.
 *
 * The terrain map stays resident in device memory for the whole trial.
 * Target SoA buffers, the work queue, and RNG state are allocated once
 * during sensor_gpu_init().
 */

/* -------------------------------------------------------------------------
 * Launch parameters. Tuned for a few thousand targets on a single GPU;
 * change these if the target count grows by several orders of magnitude.
 * ------------------------------------------------------------------------- */
static const unsigned int TRIAGE_BLOCK    = 256u;
static const unsigned int RAYMARCH_WARP   = 32u;   /* one warp per target */
static const unsigned int RAYMARCH_WARPS_PER_BLOCK = 4u;
static const unsigned int RAYMARCH_BLOCK  = RAYMARCH_WARP * RAYMARCH_WARPS_PER_BLOCK;

/* -------------------------------------------------------------------------
 * Geometry of a single sensor-to-target line, computed once during
 * triage and recomputed (cheaply) in the ray-march kernel. Grouping
 * these fields keeps function signatures readable.
 * ------------------------------------------------------------------------- */
struct ray_geometry {
    float tx, ty, ta;        /* target world position and altitude (m) */
    float dx, dy, dz;        /* sensor-to-target delta (m) */
    float d_2d;              /* horizontal distance (m) */
    float d_3d;              /* slant range (m) */
};

/* =========================================================================
 * Device helpers
 * ========================================================================= */

/* Look up terrain elevation at a world (x, y) position via the bound
 * texture. Mirrors terrain_elevation() on the CPU, but clamps to map
 * bounds rather than asserting because kernels cannot abort cleanly. */
__device__ __forceinline__
float terrain_elevation(const cudaTextureObject_t tex,
                        const unsigned int cols, const unsigned int rows,
                        const float x_scale, const float y_scale,
                        const float x_min, const float x_max,
                        const float y_min, const float y_max,
                        float x, float y)
{
    /* Clamp world position to map bounds. The texture's addressMode is
     * cudaAddressModeClamp so this is belt-and-braces, but it also keeps
     * behavior identical to the CPU side. */
    x = fmaxf(x_min, fminf(x, x_max));
    y = fmaxf(y_min, fminf(y, y_max));

    const float u = (x / x_scale) + (float)(cols / 2u) + 0.5f;
    const float v = (y / y_scale) + (float)(rows / 2u) + 0.5f;
    return tex2D<float>(tex, u, v);
}

/* Toroidal wrap of a coordinate to the map bounds. */
__device__ __forceinline__
float toroidal_wrap(float v, const float lo, const float hi)
{
    if (v > hi)      v = lo + (v - hi);
    else if (v < lo) v = hi - (lo - v);
    return v;
}

/* Place the target at its new position for this time step and look up
 * the terrain altitude beneath it. Writes tgt_x/y/alt/time back to
 * device memory so subsequent kernels and the host see the update. */
__device__ __forceinline__
void advance_target(const unsigned int tid,
                    float * __restrict__ tgt_x,
                    float * __restrict__ tgt_y,
                    float * __restrict__ tgt_alt,
                    const float * __restrict__ tgt_dir,
                    const float * __restrict__ tgt_vel,
                    float * __restrict__ tgt_time,
                    const float * __restrict__ tgt_height,
                    const float sim_time,
                    const cudaTextureObject_t terrain_tex,
                    const unsigned int terrain_cols,
                    const unsigned int terrain_rows,
                    const float terrain_x_scale, const float terrain_y_scale,
                    const float terrain_x_min, const float terrain_x_max,
                    const float terrain_y_min, const float terrain_y_max,
                    struct ray_geometry *geom)
{
    float tx = tgt_x[tid];
    float ty = tgt_y[tid];
    const float v   = tgt_vel[tid];
    const float dir = tgt_dir[tid];
    const float t0  = tgt_time[tid];

    if (v > 0.0f) {
        const float dt = sim_time - t0;
        tx = toroidal_wrap(tx + dt * v * cosf(dir),
                           terrain_x_min, terrain_x_max);
        ty = toroidal_wrap(ty + dt * v * sinf(dir),
                           terrain_y_min, terrain_y_max);
        tgt_time[tid] = sim_time;
    }

    const float ground = terrain_elevation(
        terrain_tex, terrain_cols, terrain_rows,
        terrain_x_scale, terrain_y_scale,
        terrain_x_min, terrain_x_max, terrain_y_min, terrain_y_max,
        tx, ty);
    const float ta = ground + tgt_height[tid];

    tgt_x[tid]   = tx;
    tgt_y[tid]   = ty;
    tgt_alt[tid] = ta;

    geom->tx = tx;
    geom->ty = ty;
    geom->ta = ta;
}

/* Fill in the sensor-to-target deltas given a sensor position and a
 * target position already in `geom`. */
__device__ __forceinline__
void compute_sensor_to_target(struct ray_geometry *geom,
                              const float sx, const float sy, const float sa)
{
    geom->dx = geom->tx - sx;
    geom->dy = geom->ty - sy;
    geom->dz = geom->ta - sa;
    geom->d_2d = sqrtf(geom->dx * geom->dx + geom->dy * geom->dy);
    geom->d_3d = sqrtf(geom->d_2d * geom->d_2d + geom->dz * geom->dz);
}

/* Stage 1: is the target inside the current beam's effective gate width,
 * defined as the maximum of the antenna beamwidth and the rotation per
 * dwell?
 *
 * Gating at just the 3 dB beamwidth would leave small angular gaps
 * between consecutive dwells whenever rot_inc_per_dwell > beamwidth_r.
 * Targets in those gaps would never get a detection attempt despite
 * sidelobes putting real signal on them. Widening the gate closes the
 * gap; the antenna_gain_sq function still attenuates targets at large
 * angular offsets via the Gaussian + sidelobe-floor model, so off-beam
 * targets simply get very low Pd rather than being skipped. */
__device__ __forceinline__
bool target_in_beam(const struct ray_geometry *geom,
                    const float beam_dir,
                    const float beamwidth_r,
                    const float rot_inc_r)
{
    const float half_width = 0.5f * fmaxf(beamwidth_r, rot_inc_r);

    float rel_azi = atan2f(geom->dy, geom->dx) - beam_dir;
    while (rel_azi >  (float)M_PI) rel_azi -= 2.0f * (float)M_PI;
    while (rel_azi < -(float)M_PI) rel_azi += 2.0f * (float)M_PI;
    return (fabsf(rel_azi) <= half_width);
}

/* Effective Earth-radius factor at altitude h, for an exponentially-
 * decaying refractivity atmosphere. Identical formula to the CPU
 * atmosphere_k_eff(). Inlined here so the kernels can call it from
 * within their tight loops without a function-call boundary. */
__device__ __forceinline__
float atmosphere_k_eff_dev(const float h_m, const float r_earth_m)
{
    const float dn_dh = -(atm_N0_units * 1.0e-6f / atm_scale_h_m)
                        * expf(-h_m / atm_scale_h_m);

    return 1.0f / (1.0f + r_earth_m * dn_dh);
}

/* Stage 2: is the target beyond the radar horizon? Uses k(h) evaluated
 * at the mean of sensor and target altitudes — accurate to ≈ 1 % for
 * the geometries in this scenario. */
__device__ __forceinline__
bool target_beyond_horizon(const struct ray_geometry *geom,
                           const float sa, const float r_earth)
{
    const float hs = fmaxf(0.0f, sa);
    const float ht = fmaxf(0.0f, geom->ta);

    const float h_avg = 0.5f * (hs + ht);
    const float k_eff = atmosphere_k_eff_dev(h_avg, r_earth);
    const float r_eff = k_eff * r_earth;

    const float max_dist = sqrtf(2.0f * r_eff * hs)
                         + sqrtf(2.0f * r_eff * ht);

    return (geom->d_2d > max_dist);
}

/* Stage 3: is the target outside the antenna's vertical coverage,
 * after correcting for platform roll? */
__device__ __forceinline__
bool target_outside_vertical(const struct ray_geometry *geom,
                             const float platform_hdg,
                             const float platform_rol,
                             const float elev_min, const float elev_max)
{
    const float rel_brg       = atan2f(geom->dy, geom->dx) - platform_hdg;
    const float geom_elev     = atan2f(geom->dz, geom->d_2d);
    const float apparent_elev = geom_elev - (platform_rol * sinf(rel_brg));
    return (apparent_elev < elev_min || apparent_elev > elev_max);
}

/*
 * Constant-memory biome data, used only by the probabilistic test.
 */
__constant__ float d_biome_gamma_lin[NBIOMES];   /* gamma in linear (filled at init) */
__constant__ float d_biome_elevations[NBIOMES - 1] = { 400.0f, 1000.0f };
__constant__ float d_biome_specular_max[NBIOMES];   /* filled at init */
__constant__ float d_biome_roughness_m[NBIOMES];    /* filled at init */

/* Antenna pattern gain (one-way) squared. Gaussian main lobe with a
 * fixed sidelobe floor at -30 dB (10^-3 in power). The half-width
 * is the 3 dB point of the Gaussian, set to match beamwidth_r/2. */
__device__ __forceinline__
float antenna_gain_sq(const float angular_offset_r, const float beamwidth_r)
{
    /* The 3 dB beamwidth of a Gaussian beam G(θ) = exp(-θ²/σ²)
     * corresponds to σ = beamwidth_r / (2 × sqrt(ln 2)). One-way
     * gain² = exp(-2 θ²/σ²) = exp(-2 ln 2 × (2θ/beamwidth)²). */
    const float u = 2.0f * angular_offset_r / beamwidth_r;
    const float g_sq = expf(-2.0f * 0.6931472f * u * u);
    return fmaxf(g_sq, 1.0e-3f);    /* -30 dB sidelobe floor */
}

/* Look up biome gamma (linear) from terrain altitude at a sample point.
 * Same structure as the old biome lookup, but reads from the new
 * gamma-in-linear-units array. */
__device__ __forceinline__
float biome_gamma_at_alt(const float alt_m)
{
    float g = d_biome_gamma_lin[NBIOMES - 1];
    #pragma unroll
    for (unsigned int b = 0; b < NBIOMES - 1; b++) {
        if (alt_m < d_biome_elevations[b]) {
            g = d_biome_gamma_lin[b];
            break;
        }
    }
    return g;
}

/* Integrate clutter contribution over one resolution cell of the radar.
 * The cell is an annular wedge between (range - dr/2) and (range + dr/2),
 * angular width beamwidth_r, centered on beam_dir.
 *
 * Returns the cell's total normalized return power, including range
 * attenuation 1/R^4 and antenna gain G^2 at each sample.
 *
 * The integrand is sigma_0 * G^2 * dA / R^4, where dA = dr * R * dθ
 * for the cell element at range R and angular offset θ from beam
 * center. We sample N_range × N_cross points and sum.
 *
 * For tutorial purposes the result is in arbitrary normalized units
 * (the same units as the target signal). What matters is the *ratio*
 * with the target signal and the CFAR threshold. */
__device__ __forceinline__
float integrate_cell_clutter(
    const float center_range, const float beam_dir,
    const float beamwidth_r, const float range_res_m,
    const float sx, const float sy, const float sa,
    const int n_range, const int n_cross,
    const cudaTextureObject_t terrain_tex,
    const unsigned int terrain_cols, const unsigned int terrain_rows,
    const float terrain_x_scale, const float terrain_y_scale,
    const float terrain_x_min, const float terrain_x_max,
    const float terrain_y_min, const float terrain_y_max,
    const float r_earth)
{
    const float dr = range_res_m;
    const float dtheta = beamwidth_r;
    const float dA_per_unit = (dr * dtheta) / ((float)(n_range * n_cross));

    float clutter = 0.0f;

    for (int i = 0; i < n_range; i++) {
        const float t_r = ((float)i + 0.5f) / (float)n_range;    /* (0, 1) */
        const float R = center_range - 0.5f * dr + t_r * dr;
        const float R_safe = fmaxf(1.0f, R);
        const float inv_R4 = 1.0f / (R_safe * R_safe * R_safe * R_safe);

        for (int j = 0; j < n_cross; j++) {
            const float t_c   = ((float)j + 0.5f) / (float)n_cross;
            const float dtheta_off = -0.5f * beamwidth_r + t_c * beamwidth_r;
            const float az = beam_dir + dtheta_off;

            /* World position of this sample on the ground (flat-Earth
             * plane). The Earth-curvature drop would lift the actual
             * sample above the local terrain by R^2/(2*k*R_earth),
             * which matters for grazing-angle calculation but not for
             * the world (x, y) where we look up biome. */
            const float wx = sx + R_safe * cosf(az);
            const float wy = sy + R_safe * sinf(az);

            /* Clamp to map bounds. */
            const float wx_c = fmaxf(terrain_x_min, fminf(wx, terrain_x_max));
            const float wy_c = fmaxf(terrain_y_min, fminf(wy, terrain_y_max));

            /* Terrain altitude at the sample. */
            const float terr = terrain_elevation(
                terrain_tex, terrain_cols, terrain_rows,
                terrain_x_scale, terrain_y_scale,
                terrain_x_min, terrain_x_max,
                terrain_y_min, terrain_y_max,
                wx_c, wy_c);

            /* Effective height of sensor above the sample's local
             * ground level, accounting for Earth curvature drop. */
            const float earth_drop = (R_safe * R_safe)
                                   / (2.0f * 1.33f * r_earth);
            const float h_sensor_eff = sa - terr - earth_drop;
            if (h_sensor_eff <= 0.0f) continue;   /* sample below horizon */

            /* Grazing angle. sin(grazing) = h_eff / R for small angles. */
            const float sin_graze = fminf(1.0f, h_sensor_eff / R_safe);

            /* Constant-gamma model: sigma_0 = gamma * sin(grazing). */
            const float gamma = biome_gamma_at_alt(terr);
            const float sigma_0 = gamma * sin_graze;

            /* Antenna gain^2 at this angular offset from beam center. */
            const float g_sq = antenna_gain_sq(dtheta_off, beamwidth_r);

            /* Area of this sub-cell (annular wedge element). */
            const float dA = R_safe * dA_per_unit;

            /* Clutter return: sigma_0 * G^2 * dA / R^4. */
            clutter += sigma_0 * g_sq * dA * inv_R4;
        }
    }

    return clutter;
}

/* CA-CFAR threshold. Integrate clutter over 2 * n_ref reference cells,
 * skipping n_guard cells on each side of the test cell. Multiply the
 * mean by cfar_alpha to set the threshold.
 *
 * Returns the threshold *energy* in the same units as the target signal
 * and test-cell clutter. */
__device__ __forceinline__
float cfar_threshold(
    const float target_range, const float beam_dir,
    const float sx, const float sy, const float sa,
    const struct radar_params *radar,
    const float beamwidth_r,
    const float clutter_scale,
    const cudaTextureObject_t terrain_tex,
    const unsigned int terrain_cols, const unsigned int terrain_rows,
    const float terrain_x_scale, const float terrain_y_scale,
    const float terrain_x_min, const float terrain_x_max,
    const float terrain_y_min, const float terrain_y_max,
    const float r_earth)
{
    const float dr = radar->range_res_m;
    const int   n_ref = radar->cfar_n_ref;
    const int   n_guard = radar->cfar_n_guard;

    float sum = 0.0f;
    int   n_used = 0;

    for (int k = 1; k <= n_ref; k++) {
        const int range_offset_cells = n_guard + k;

        /* Reference cell on the far side. */
        const float R_far = target_range + (float)range_offset_cells * dr;
        sum += integrate_cell_clutter(
            R_far, beam_dir, beamwidth_r, dr,
            sx, sy, sa,
            radar->cell_grid_n_range, radar->cell_grid_n_cross,
            terrain_tex, terrain_cols, terrain_rows,
            terrain_x_scale, terrain_y_scale,
            terrain_x_min, terrain_x_max,
            terrain_y_min, terrain_y_max,
            r_earth);
        n_used++;

        /* Reference cell on the near side. Skip if it would fall
         * below 1 km (the radar's near-range cutoff). */
        const float R_near = target_range - (float)range_offset_cells * dr;
        if (R_near > 1000.0f) {
            sum += integrate_cell_clutter(
                R_near, beam_dir, beamwidth_r, dr,
                sx, sy, sa,
                radar->cell_grid_n_range, radar->cell_grid_n_cross,
                terrain_tex, terrain_cols, terrain_rows,
                terrain_x_scale, terrain_y_scale,
                terrain_x_min, terrain_x_max,
                terrain_y_min, terrain_y_max,
                r_earth);
            n_used++;
        }
    }

    const float mean_clutter = (n_used > 0) ? (sum / (float)n_used) : 0.0f;
    return radar->cfar_alpha * (clutter_scale * mean_clutter + radar->noise_floor_norm);
}

/*
 * Two-way multipath interference factor for the target signal.
 *
 * Returns a multiplier in [~0, ~16] to be applied to E_target. A value
 * of 1 means no multipath effect (rough biome or invalid geometry);
 * values above 1 mean constructive interference, below 1 destructive.
 *
 * The specular point is iterated once: an initial estimate using flat
 * ground at altitude 0, then refined using the actual ground altitude
 * at that estimate. The bounce path is assumed unobstructed; for the
 * typical AWACS-vs-ground geometry the direct and bounce paths run
 * through nearly the same airspace, so if the direct path passed the
 * terrain-shielding check, the bounce path is almost certainly clear.
 */
__device__ __forceinline__
float multipath_gain_dev(const float sx, const float sy, const float sa,
                         const float tx, const float ty, const float ta,
                         const float d_2d,
                         const cudaTextureObject_t terrain_tex,
                         const unsigned int terrain_cols,
                         const unsigned int terrain_rows,
                         const float terrain_x_scale, const float terrain_y_scale,
                         const float terrain_x_min, const float terrain_x_max,
                         const float terrain_y_min, const float terrain_y_max)
{
    /* First-pass specular point assuming ground at altitude 0. */
    const float t0 = sa / (sa + ta);
    float spec_x = sx + (tx - sx) * t0;
    float spec_y = sy + (ty - sy) * t0;

    spec_x = fmaxf(terrain_x_min, fminf(spec_x, terrain_x_max));
    spec_y = fmaxf(terrain_y_min, fminf(spec_y, terrain_y_max));

    float ground_alt = terrain_elevation(
        terrain_tex, terrain_cols, terrain_rows,
        terrain_x_scale, terrain_y_scale,
        terrain_x_min, terrain_x_max, terrain_y_min, terrain_y_max,
        spec_x, spec_y);

    /* Re-derive specular point above actual ground. Degenerate if ground
     * is above either endpoint. */
    const float h_s_eff = sa - ground_alt;
    const float h_t_eff = ta - ground_alt;
    if (h_s_eff <= 1.0f || h_t_eff <= 1.0f) {
        return 1.0f;
    }
    const float t1 = h_s_eff / (h_s_eff + h_t_eff);
    spec_x = sx + (tx - sx) * t1;
    spec_y = sy + (ty - sy) * t1;
    spec_x = fmaxf(terrain_x_min, fminf(spec_x, terrain_x_max));
    spec_y = fmaxf(terrain_y_min, fminf(spec_y, terrain_y_max));
    ground_alt = terrain_elevation(
        terrain_tex, terrain_cols, terrain_rows,
        terrain_x_scale, terrain_y_scale,
        terrain_x_min, terrain_x_max, terrain_y_min, terrain_y_max,
        spec_x, spec_y);

    const float h_s = sa - ground_alt;
    const float h_t = ta - ground_alt;
    if (h_s <= 1.0f || h_t <= 1.0f) {
        return 1.0f;
    }

    /* Exact path lengths. */
    const float d_direct = sqrtf(d_2d * d_2d + (h_s - h_t) * (h_s - h_t));
    const float d_b1 = sqrtf((t1 * d_2d) * (t1 * d_2d) + h_s * h_s);
    const float d_b2 = sqrtf(((1.0f - t1) * d_2d) * ((1.0f - t1) * d_2d)
                              + h_t * h_t);
    const float d_bounce = d_b1 + d_b2;
    const float delta_d = d_bounce - d_direct;

    /* Grazing angle at the specular point. */
    const float sin_graze = h_s / fmaxf(1.0f, d_b1);

    /* Biome parameters at the specular point. */
    float gamma_max = d_biome_specular_max[NBIOMES - 1];
    float sigma_h   = d_biome_roughness_m[NBIOMES - 1];
    #pragma unroll
    for (unsigned int b = 0; b < NBIOMES - 1; b++) {
        if (ground_alt < d_biome_elevations[b]) {
            gamma_max = d_biome_specular_max[b];
            sigma_h   = d_biome_roughness_m[b];
            break;
        }
    }

    /* Rayleigh roughness suppression. */
    const float roughness_arg = (4.0f * (float)M_PI * sigma_h * sin_graze)
                              / radar_wavelength_m;
    const float gamma_eff = gamma_max * expf(-roughness_arg * roughness_arg);

    if (gamma_eff < 0.01f) {
        return 1.0f;
    }

    /* Phase difference, with the π flip on reflection. */
    const float phase = 2.0f * (float)M_PI * delta_d / radar_wavelength_m
                      + (float)M_PI;

    /* One-way interference factor. */
    const float g_one_way = 1.0f + gamma_eff * gamma_eff
                          + 2.0f * gamma_eff * cosf(phase);

    /* Two-way (round-trip) factor. Floor to avoid exactly-zero at
     * perfect nulls. */
    const float g_two_way = g_one_way * g_one_way;

    return fmaxf(g_two_way, 1.0e-3f);
}

/* Probabilistic detection in ground clutter. Returns the final
 * target_detect_state (DETECTED or MISSED) and updates the persistent
 * "ever detected" flag if appropriate. */
/* Detection decision for one illuminating dwell. Returns DETECTED or
 * MISSED; sets the sticky `detected` flag if appropriate.
 *
 * Pipeline:
 *   1. Compute antenna gain on the target.
 *   2. Compute target signal energy with antenna gain and pulse
 *      integration boost.
 *   3. Integrate clutter in the target's test cell.
 *   4. Integrate clutter in 2*N_ref reference cells; derive CFAR
 *      threshold from the mean.
 *   5. SINR = E_target / (E_clutter_test + noise_floor).
 *   6. Detection if (E_target + E_clutter_test + noise_floor) > threshold;
 *      probabilistic logistic on the margin to capture fluctuation.
 *
 * Step 6 is where Swerling-model statistics would refine the
 * probabilistic test. The logistic-curve approximation here is good
 * enough for tutorial purposes and matches the old code's style. */
__device__ __forceinline__
enum target_detect_state probabilistic_detection(
    const struct ray_geometry *geom,
    const float rcs,
    const float ref_range_m, const float ref_rcs_m2,
    const int target_mode,
    const float vel_ms,
    const float dir_r,
    const float beam_dir,
    const float beamwidth_r,
    const float sx, const float sy, const float sa,
    const struct radar_params *radar,
    const cudaTextureObject_t terrain_tex,
    const unsigned int terrain_cols, const unsigned int terrain_rows,
    const float terrain_x_scale, const float terrain_y_scale,
    const float terrain_x_min, const float terrain_x_max,
    const float terrain_y_min, const float terrain_y_max,
    const float r_earth,
    curandStatePhilox4_32_10_t *rng_state,
    uint8_t *detected_slot)
{
    const float r = fmaxf(1.0f, geom->d_3d);
    const float target_az = atan2f(geom->dy, geom->dx);

    /* Angular offset of target from beam center; wrap to (-pi, pi]. */
    float target_offset = target_az - beam_dir;
    while (target_offset >  (float)M_PI) target_offset -= 2.0f * (float)M_PI;
    while (target_offset < -(float)M_PI) target_offset += 2.0f * (float)M_PI;

    /* Antenna gain on the target. */
    const float g_target_sq = antenna_gain_sq(target_offset, beamwidth_r);

    /* Two-way multipath interference. The lobe pattern depends on
     * target altitude, range, and ground reflectivity at the specular
     * point — the dominant altitude-dependent effect against
     * low-altitude targets over smooth biomes. */
    const float g_mp = multipath_gain_dev(
        sx, sy, sa,
        geom->tx, geom->ty, geom->ta,
        geom->d_2d,
        terrain_tex, terrain_cols, terrain_rows,
        terrain_x_scale, terrain_y_scale,
        terrain_x_min, terrain_x_max,
        terrain_y_min, terrain_y_max);

    /* --- Doppler / clutter-notch visibility --------------------------------
     * A ground target shares range/azimuth with its clutter, so the only
     * Doppler separation is the target's own radial ground velocity. A
     * stationary target sits in the notch and is cancelled with the clutter;
     * a mover in a clear bin is seen against the suppressed clutter residual.
     * Per-state activity Doppler (machinery, launch plume) gives staging and
     * firing a floor independent of bulk motion. Index = enum target_mode
     * (HIDING, STAGING, FIRING, DRIVING). */
    const float activity_doppler[4] = { 0.0f, 5.0f, 1.0e6f, 2.0f };
    const float v_radial  = vel_ms * cosf(dir_r - target_az);
    const float v_eff     = fmaxf(fabsf(v_radial), activity_doppler[target_mode]);
    const float vmdv      = radar->mdv_ms;
    const float vis       = (v_eff * v_eff) / (v_eff * v_eff + vmdv * vmdv);
    const float clutter_f = 1.0f - vis * (1.0f - 1.0f / radar->mti_improvement);

    /* Target signal energy: free-space SNR scaled by antenna gain^2, pulse
     * integration gain, multipath, and the Doppler filter response. */
    const float range_ratio = ref_range_m / r;
    const float r2 = range_ratio * range_ratio;
    const float snr_base = r2 * r2 * (rcs / ref_rcs_m2);

    const float pulse_int_gain = 0.7f * sqrtf((float)radar->n_pulses_per_dwell);
    const float E_target = snr_base * g_target_sq * pulse_int_gain * g_mp * vis;

    /* Clutter in the test cell: full in the notch, MTI residual in a clear bin. */
    const float E_clutter_test = clutter_f * integrate_cell_clutter(
        geom->d_2d, beam_dir, beamwidth_r, radar->range_res_m,
        sx, sy, sa,
        radar->cell_grid_n_range, radar->cell_grid_n_cross,
        terrain_tex, terrain_cols, terrain_rows,
        terrain_x_scale, terrain_y_scale,
        terrain_x_min, terrain_x_max,
        terrain_y_min, terrain_y_max,
        r_earth);

    /* CFAR threshold from reference cells, with the same clutter suppression
     * (the reference cells are in the target's Doppler bin). */
    const float T = cfar_threshold(
        geom->d_2d, beam_dir,
        sx, sy, sa,
        radar, beamwidth_r,
        clutter_f,
        terrain_tex, terrain_cols, terrain_rows,
        terrain_x_scale, terrain_y_scale,
        terrain_x_min, terrain_x_max,
        terrain_y_min, terrain_y_max,
        r_earth);

    /* Test cell total: target + clutter + noise. Threshold passed if
     * total exceeds T. The logistic-curve smoothing captures
     * fluctuation about that boundary. */
    const float test_cell = E_target + E_clutter_test + radar->noise_floor_norm;
    const float margin = (test_cell - T) / fmaxf(T, radar->noise_floor_norm);

    /* Logistic on the margin: steep transition around margin = 0. */
    const float pd = 1.0f / (1.0f + expf(-3.0f * margin));

    if (curand_uniform(rng_state) < pd) {
        if (target_mode != FIRING) {
            *detected_slot = 1;
        }
        return DETECTED;
    }
    return MISSED;
}

/* =========================================================================
 * Kernel 1: triage
 * One thread per target. Runs the cheap rejection tests and either
 * writes a final detection state or enqueues the target index for the
 * ray-march kernel.
 * ========================================================================= */
__global__
void triage_kernel(
    /* Target SoA — position fields are read and written, others read only */
    float * __restrict__ tgt_x,    float * __restrict__ tgt_y,
    float * __restrict__ tgt_alt,
    const float * __restrict__ tgt_dir,
    const float * __restrict__ tgt_vel,
    float * __restrict__ tgt_time,
    const float * __restrict__ tgt_height,
    const float * __restrict__ tgt_rcs,
    const int   * __restrict__ tgt_mode,
    /* Detection outputs */
    int     * __restrict__ tds_out,
    uint8_t * __restrict__ detected_out,
    /* Ray-march work queue: three parallel arrays */
    int * __restrict__ queue_tid,
    int * __restrict__ queue_n_dwells,
    int * __restrict__ queue_first_dwell,
    int * __restrict__ queue_count,
    /* Per-target RNG state */
    curandStatePhilox4_32_10_t *rng_states,
    /* Sensor / platform geometry (uniform) */
    const float sx, const float sy, const float sa,
    const float platform_hdg, const float platform_rol,
    const float base_dir, const float rot_inc_per_dwell,
    const float beamwidth_r,
    const float sim_time,
    const float r_earth,
    const float elev_min, const float elev_max,
    const float ref_range_m, const float ref_rcs_m2,
    /* Terrain access */
    const cudaTextureObject_t terrain_tex,
    const unsigned int terrain_cols, const unsigned int terrain_rows,
    const float terrain_x_scale, const float terrain_y_scale,
    const float terrain_x_min, const float terrain_x_max,
    const float terrain_y_min, const float terrain_y_max,
    /* Bounds */
    const unsigned int n_targets)
{
    const unsigned int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= n_targets) return;

    /* Once per step: position update, terrain altitude, sensor-relative
     * geometry. Target motion across the n_dwells_per_step interval is
     * sub-meter for ground vehicles, so we use the post-update position
     * for every dwell. The sensor moves perhaps 10 m in the same window,
     * negligible against >100 km ranges. */
    struct ray_geometry geom;
    advance_target(tid,
                   tgt_x, tgt_y, tgt_alt, tgt_dir, tgt_vel, tgt_time, tgt_height,
                   sim_time,
                   terrain_tex, terrain_cols, terrain_rows,
                   terrain_x_scale, terrain_y_scale,
                   terrain_x_min, terrain_x_max,
                   terrain_y_min, terrain_y_max,
                   &geom);
    compute_sensor_to_target(&geom, sx, sy, sa);

    /* Stage 2: horizon. Beam-direction-independent, done once. */
    if (target_beyond_horizon(&geom, sa, r_earth)) {
        tds_out[tid] = BEYOND_HORIZON;
        return;
    }

    /* Stage 3: vertical lobe / nadir hole. Also beam-direction-independent. */
    if (target_outside_vertical(&geom, platform_hdg, platform_rol,
                                elev_min, elev_max)) {
        tds_out[tid] = NADIR_HOLE;
        return;
    }

    /* Stage 1 (per dwell): scan the n_dwells_per_step antenna positions,
     * record the first illuminating dwell index and count the total.
     * A 1.4° beam rotating at 6 RPM covers ~1.44° per dwell, so most
     * illuminated targets are hit by exactly one dwell; occasionally
     * two; rarely more. */
    int n_illuminated = 0;
    int first_dwell   = -1;
    for (unsigned int d = 0; d < n_dwells_per_step; d++) {
        const float beam_dir = base_dir + (float)d * rot_inc_per_dwell;
        if (target_in_beam(&geom, beam_dir, beamwidth_r, rot_inc_per_dwell)) {
            if (first_dwell < 0) first_dwell = (int)d;
            n_illuminated++;
        }
    }

    if (n_illuminated == 0) {
        /* Target not visited this step — leave tds_out unchanged. */
        return;
    }

    /* Enqueue for the ray-march kernel. Geometry doesn't change across
     * the step's dwells, so one ray-march per target suffices regardless
     * of n_illuminated. The ray-march kernel will run that many
     * probabilistic-detection draws on the survivors, using the first
     * illuminating dwell index to reconstruct each draw's beam direction. */
    const int slot = atomicAdd(queue_count, 1);
    queue_tid[slot]         = (int)tid;
    queue_n_dwells[slot]    = n_illuminated;
    queue_first_dwell[slot] = first_dwell;
}

/* =========================================================================
 * Kernel 2: ray-march
 * One warp per queued target. blockDim.x must be a multiple of 32.
 * ========================================================================= */
__global__
void raymarch_kernel(
    /* Work queue */
    const int * __restrict__ queue_tid,
    const int * __restrict__ queue_n_dwells,
    const int * __restrict__ queue_count,
    /* Per-target queue: which dwell index first illuminated each
     * queued target. The raymarch loops illumination draws starting
     * from that dwell index using rot_inc_per_dwell. */
    const int * __restrict__ queue_first_dwell,
    /* Target SoA — read only here */
    const float * __restrict__ tgt_x,
    const float * __restrict__ tgt_y,
    const float * __restrict__ tgt_alt,
    const float * __restrict__ tgt_rcs,
    const float * __restrict__ tgt_dir,
    const float * __restrict__ tgt_vel,
    const int   * __restrict__ tgt_mode,
    /* Detection outputs */
    int     * __restrict__ tds_out,
    uint8_t * __restrict__ detected_out,
    /* Per-target RNG state */
    curandStatePhilox4_32_10_t *rng_states,
    /* Sensor geometry */
    const float sx, const float sy, const float sa,
    const float r_earth,
    const float ref_range_m, const float ref_rcs_m2,
    /* Beam scanning (for clutter integration per dwell) */
    const float base_dir,
    const float rot_inc_per_dwell,
    const float beamwidth_r,
    /* Radar parameters */
    const struct radar_params radar,
    /* Terrain access */
    const cudaTextureObject_t terrain_tex,
    const unsigned int terrain_cols, const unsigned int terrain_rows,
    const float terrain_x_scale, const float terrain_y_scale,
    const float terrain_x_min, const float terrain_x_max,
    const float terrain_y_min, const float terrain_y_max)
{
    const unsigned int warp_id_in_block = threadIdx.x / RAYMARCH_WARP;
    const unsigned int warp_id          = blockIdx.x * RAYMARCH_WARPS_PER_BLOCK
                                            + warp_id_in_block;
    const unsigned int lane             = threadIdx.x % RAYMARCH_WARP;

    if ((int)warp_id >= *queue_count) return;

    const int tid         = queue_tid[warp_id];
    const int n_dwells    = queue_n_dwells[warp_id];
    const int first_dwell = queue_first_dwell[warp_id];

    /* Geometry reconstruction (same as before). */
    struct ray_geometry geom;
    if (lane == 0) {
        geom.tx = tgt_x[tid];
        geom.ty = tgt_y[tid];
        geom.ta = tgt_alt[tid];
    }
    geom.tx = __shfl_sync(0xffffffff, geom.tx, 0);
    geom.ty = __shfl_sync(0xffffffff, geom.ty, 0);
    geom.ta = __shfl_sync(0xffffffff, geom.ta, 0);
    compute_sensor_to_target(&geom, sx, sy, sa);

    /* March the ray from the sensor toward the target with steps no
     * larger than half a terrain cell. Lanes cover 32 consecutive
     * sample points; this is the access pattern the texture cache
     * is optimized for. The refracted ray sags below the chord by
     *   bulge(s) = s * (d_2d - s) / (2 * k(h_chord) * R_earth)
     * which we subtract from the chord altitude at each step.
     *
     * The loop iterates in warp-wide rounds: every round advances
     * `base` by RAYMARCH_WARP and each lane handles point `base+lane`.
     * All 32 lanes execute every round and reach the __all_sync /
     * __any_sync calls together, even on the final partial round —
     * lanes whose index is past num_steps contribute neutral values
     * (my_clear = 1, no shielding). This is required for correctness
     * on Volta+ : if lanes leave the loop at different iterations,
     * the warp-collective votes with mask 0xffffffff deadlock. */
    const float step_size = fminf(terrain_x_scale, terrain_y_scale) * 0.5f;
    const int   num_steps = (int)(geom.d_2d / step_size);

    int shielded = 0;
    if (num_steps >= 1) {
        const float terrain_ceiling = terrain_max * 1.1f;
        const float inv_N = 1.0f / (float)num_steps;
        const float two_r_earth = 2.0f * r_earth;
        int shielded_local = 0;

        for (int base = 1; base < num_steps; base += (int)RAYMARCH_WARP) {
            const int i = base + (int)lane;

            /* Inactive lanes (i past the end) look "clear" so they
             * neither block the all-clear early-out nor shield. */
            int my_clear = 1;
            if (i < num_steps) {
                const float t = 1.0f - (float)i * inv_N;
                const float cx = sx + geom.dx * t;
                const float cy = sy + geom.dy * t;
                const float ca_chord = sa + geom.dz * t;

                const float s_from_sensor = t * geom.d_2d;
                const float s_from_target = geom.d_2d - s_from_sensor;
                const float k_local = atmosphere_k_eff_dev(ca_chord, r_earth);
                const float bulge   = (s_from_sensor * s_from_target)
                                    / (two_r_earth * k_local);
                const float ca = ca_chord - bulge;

                my_clear = (ca > terrain_ceiling);
                if (!my_clear) {
                    const float terr = terrain_elevation(
                        terrain_tex, terrain_cols, terrain_rows,
                        terrain_x_scale, terrain_y_scale,
                        terrain_x_min, terrain_x_max,
                        terrain_y_min, terrain_y_max,
                        cx, cy);
                    if (ca < terr) shielded_local = 1;
                }
            }

            /* Both votes are now executed by all 32 lanes every round. */
            if (__all_sync(0xffffffff, my_clear))       break;
            if (__any_sync(0xffffffff, shielded_local)) break;
        }

        shielded = __any_sync(0xffffffff, shielded_local);
    }

    /* Lane 0 writes the result. For each of the n_dwells illuminating
     * dwells (consecutive starting at first_dwell), run the new
     * probabilistic detection with that dwell's beam pointing. */
    if (lane == 0) {
        if (shielded) {
            tds_out[tid] = TERRAIN_SHIELDED;
        }
        else {
            enum target_detect_state last = MISSED;
            for (int d = 0; d < n_dwells; d++) {
                const float beam_dir = base_dir
                    + (float)(first_dwell + d) * rot_inc_per_dwell;
                last = probabilistic_detection(
                    &geom, tgt_rcs[tid], ref_range_m, ref_rcs_m2,
                    tgt_mode[tid],
                    tgt_vel[tid], tgt_dir[tid],
                    beam_dir, beamwidth_r,
                    sx, sy, sa,
                    &radar,
                    terrain_tex, terrain_cols, terrain_rows,
                    terrain_x_scale, terrain_y_scale,
                    terrain_x_min, terrain_x_max,
                    terrain_y_min, terrain_y_max,
                    r_earth,
                    &rng_states[tid], &detected_out[tid]);
            }
            tds_out[tid] = (int)last;
        }
    }
}

/* =========================================================================
 * RNG seeding kernel
 * ========================================================================= */
__global__
void rng_init_kernel(curandStatePhilox4_32_10_t *states,
                     const unsigned int n_targets,
                     const uint64_t seed)
{
    const unsigned int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= n_targets) return;
    curand_init(seed, tid, 0, &states[tid]);
}

/* =========================================================================
 * Host-side API
 * ========================================================================= */

extern "C"
void sensor_gpu_load(void)
{
    /* Convert biome_gamma_db (host, declared in tut_5_2.h) to linear
     * and upload along with the multipath biome constants. */
    float host_gamma_lin[NBIOMES];
    for (int b = 0; b < NBIOMES; b++) {
        host_gamma_lin[b] = powf(10.0f, biome_gamma_db[b] / 10.0f);
    }

    CUDA_CHECK(cudaMemcpyToSymbol(d_biome_gamma_lin, host_gamma_lin,
                                  NBIOMES * sizeof(float)));
    CUDA_CHECK(cudaMemcpyToSymbol(d_biome_specular_max, biome_specular_max,
                                  NBIOMES * sizeof(float)));
    CUDA_CHECK(cudaMemcpyToSymbol(d_biome_roughness_m, biome_roughness_m,
                                  NBIOMES * sizeof(float)));
}

extern "C"
void sensor_gpu_init(struct sensor_gpu_state *gpu,
                     const unsigned int n_targets,
                     const uint64_t rng_seed,
                     const struct terrain *terp,
                     cudaStream_t cstream)
{
    gpu->n_targets = n_targets;
    gpu->terrain   = terp;
    gpu->stream = cstream;

    /* Target SoA in one allocation. */
    const size_t fbytes = (size_t)n_targets * sizeof(float);
    float *d_soa = NULL;
    CUDA_CHECK(cudaMalloc((void **)&d_soa, 8 * fbytes));
    gpu->d_x      = d_soa + 0 * n_targets;
    gpu->d_y      = d_soa + 1 * n_targets;
    gpu->d_alt    = d_soa + 2 * n_targets;
    gpu->d_dir    = d_soa + 3 * n_targets;
    gpu->d_vel    = d_soa + 4 * n_targets;
    gpu->d_time   = d_soa + 5 * n_targets;
    gpu->d_height = d_soa + 6 * n_targets;
    gpu->d_rcs    = d_soa + 7 * n_targets;

    /* Detection results, persistent across steps. */
    CUDA_CHECK(cudaMalloc(&gpu->d_tds,      n_targets * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&gpu->d_detected, n_targets * sizeof(uint8_t)));
    CUDA_CHECK(cudaMalloc(&gpu->d_mode,     n_targets * sizeof(int)));
    CUDA_CHECK(cudaMemsetAsync(gpu->d_tds,      0, n_targets * sizeof(int),     gpu->stream));
    CUDA_CHECK(cudaMemsetAsync(gpu->d_detected, 0, n_targets * sizeof(uint8_t), gpu->stream));

    /* Work queue. Worst case every target needs a ray-march. */
    CUDA_CHECK(cudaMalloc(&gpu->d_queue,       n_targets * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&gpu->d_queue_n_dwells, n_targets * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&gpu->d_queue_first_dwell, n_targets * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&gpu->d_queue_count, sizeof(int)));

    /* RNG state. */
    CUDA_CHECK(cudaMalloc(&gpu->d_rng,
                          n_targets * sizeof(curandStatePhilox4_32_10_t)));

    /* Pinned host scratch. */
    CUDA_CHECK(cudaHostAlloc(&gpu->h_pinned_upload,   8 * fbytes,
                             cudaHostAllocDefault));
    CUDA_CHECK(cudaHostAlloc(&gpu->h_pinned_results,
                             n_targets * (sizeof(int) + sizeof(uint8_t)),
                             cudaHostAllocDefault));

    /* Seed the per-target RNG states once. */
    const unsigned int grid = (n_targets + TRIAGE_BLOCK - 1) / TRIAGE_BLOCK;
    rng_init_kernel<<<grid, TRIAGE_BLOCK, 0, gpu->stream>>>(
        (curandStatePhilox4_32_10_t *)gpu->d_rng, n_targets, rng_seed);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaStreamSynchronize(gpu->stream));
}

extern "C"
void sensor_gpu_terminate(struct sensor_gpu_state *gpu)
{
    /* d_x is the head of the 8-field SoA allocation; freeing it once
     * releases all eight fields. */
    CUDA_CHECK(cudaFree(gpu->d_x));
    CUDA_CHECK(cudaFree(gpu->d_tds));
    CUDA_CHECK(cudaFree(gpu->d_detected));
    CUDA_CHECK(cudaFree(gpu->d_mode));
    CUDA_CHECK(cudaFree(gpu->d_queue));
    CUDA_CHECK(cudaFree(gpu->d_queue_n_dwells));
    CUDA_CHECK(cudaFree(gpu->d_queue_first_dwell));
    CUDA_CHECK(cudaFree(gpu->d_queue_count));
    CUDA_CHECK(cudaFree(gpu->d_rng));
    CUDA_CHECK(cudaFreeHost(gpu->h_pinned_upload));
    CUDA_CHECK(cudaFreeHost(gpu->h_pinned_results));
}

/* One simulation step. Synchronous from the caller's point of view:
 * tds_out and detected_out are valid on return. */
extern "C"
void sensor_gpu_step(struct sensor_gpu_state *gpu,
                     const float *x,      const float *y,      const float *alt,
                     const float *dir,    const float *vel,    const float *time_s,
                     const float *height, const float *rcs,    const int *mode,
                     const float sx, const float sy, const float sa,
                     const float platform_hdg, const float platform_rol,
                     const float base_dir, const float rot_inc_per_dwell,
                     const float beamwidth_r,
                     const float sim_time,
                     const float r_earth,
                     const float elev_min, const float elev_max,
                     const float ref_range_m, const float ref_rcs_m2,
                     const struct radar_params *radar,
                     int *tds_out, uint8_t *detected_out)
{
    const unsigned int n = gpu->n_targets;
    const size_t fbytes = (size_t)n * sizeof(float);
    const struct terrain *terp = gpu->terrain;

    /* Pack the 8-field SoA into the pinned upload buffer. */
    float *p = (float *)gpu->h_pinned_upload;
    memcpy(p + 0 * n, x,      fbytes);
    memcpy(p + 1 * n, y,      fbytes);
    memcpy(p + 2 * n, alt,    fbytes);
    memcpy(p + 3 * n, dir,    fbytes);
    memcpy(p + 4 * n, vel,    fbytes);
    memcpy(p + 5 * n, time_s, fbytes);
    memcpy(p + 6 * n, height, fbytes);
    memcpy(p + 7 * n, rcs,    fbytes);

    /* Upload SoA, mode, and zero the queue counter. */
    CUDA_CHECK(cudaMemcpyAsync(gpu->d_x, p, 8 * fbytes,
                               cudaMemcpyHostToDevice, gpu->stream));
    CUDA_CHECK(cudaMemcpyAsync(gpu->d_mode, mode, n * sizeof(int),
                               cudaMemcpyHostToDevice, gpu->stream));
    CUDA_CHECK(cudaMemsetAsync(gpu->d_queue_count, 0, sizeof(int), gpu->stream));

    /* Triage kernel: one thread per target. */
    const unsigned int triage_grid = (n + TRIAGE_BLOCK - 1) / TRIAGE_BLOCK;
    triage_kernel<<<triage_grid, TRIAGE_BLOCK, 0, gpu->stream>>>(
        (float *)gpu->d_x, (float *)gpu->d_y, (float *)gpu->d_alt,
        (const float *)gpu->d_dir, (const float *)gpu->d_vel,
        (float *)gpu->d_time, (const float *)gpu->d_height,
        (const float *)gpu->d_rcs, (const int *)gpu->d_mode,
        (int *)gpu->d_tds, (uint8_t *)gpu->d_detected,
        (int *)gpu->d_queue, (int *)gpu->d_queue_n_dwells,
        (int *)gpu->d_queue_first_dwell,
        (int *)gpu->d_queue_count,
        (curandStatePhilox4_32_10_t *)gpu->d_rng,
        sx, sy, sa, platform_hdg, platform_rol,
        base_dir, rot_inc_per_dwell, beamwidth_r,
        sim_time,
        r_earth, elev_min, elev_max, ref_range_m, ref_rcs_m2,
        (cudaTextureObject_t)terp->tex,
        terp->cols, terp->rows,
        terp->x_scale, terp->y_scale,
        terp->x_min, terp->x_max, terp->y_min, terp->y_max,
        n);
    CUDA_CHECK(cudaGetLastError());

    /* Ray-march kernel: one warp per queued target. */
    const unsigned int max_warps = n;
    const unsigned int rm_grid   = (max_warps + RAYMARCH_WARPS_PER_BLOCK - 1)
                                   / RAYMARCH_WARPS_PER_BLOCK;
    raymarch_kernel<<<rm_grid, RAYMARCH_BLOCK, 0, gpu->stream>>>(
        (const int *)gpu->d_queue,
        (const int *)gpu->d_queue_n_dwells,
        (const int *)gpu->d_queue_count,
        (const int *)gpu->d_queue_first_dwell,
        (const float *)gpu->d_x, (const float *)gpu->d_y,
        (const float *)gpu->d_alt, (const float *)gpu->d_rcs,
        (const float *)gpu->d_dir, (const float *)gpu->d_vel,
        (const int *)gpu->d_mode,
        (int *)gpu->d_tds, (uint8_t *)gpu->d_detected,
        (curandStatePhilox4_32_10_t *)gpu->d_rng,
        sx, sy, sa, r_earth, ref_range_m, ref_rcs_m2,
        base_dir, rot_inc_per_dwell, beamwidth_r,
        *radar,                          /* pass-by-value */
        (cudaTextureObject_t)gpu->terrain->tex,
        gpu->terrain->cols, gpu->terrain->rows,
        gpu->terrain->x_scale, gpu->terrain->y_scale,
        gpu->terrain->x_min, gpu->terrain->x_max,
        gpu->terrain->y_min, gpu->terrain->y_max);
    CUDA_CHECK(cudaGetLastError());

    /* Download results and updated positions. */
    CUDA_CHECK(cudaMemcpyAsync(tds_out,      gpu->d_tds,
                               n * sizeof(int),     cudaMemcpyDeviceToHost,
                               gpu->stream));
    CUDA_CHECK(cudaMemcpyAsync(detected_out, gpu->d_detected,
                               n * sizeof(uint8_t), cudaMemcpyDeviceToHost,
                               gpu->stream));
    CUDA_CHECK(cudaMemcpyAsync((void *)x,   gpu->d_x,   fbytes,
                               cudaMemcpyDeviceToHost, gpu->stream));
    CUDA_CHECK(cudaMemcpyAsync((void *)y,   gpu->d_y,   fbytes,
                               cudaMemcpyDeviceToHost, gpu->stream));
    CUDA_CHECK(cudaMemcpyAsync((void *)alt, gpu->d_alt, fbytes,
                               cudaMemcpyDeviceToHost, gpu->stream));

    CUDA_CHECK(cudaStreamSynchronize(gpu->stream));
}
