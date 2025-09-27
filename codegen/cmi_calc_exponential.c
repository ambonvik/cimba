/*
 * cmi_calc_exponential.c - program to calculate the ziggurat lookup tables for
 * unit exponential distribution, see
 *   https://en.wikipedia.org/wiki/Ziggurat_algorithm#McFarland's_variation
 *
 * Note that this implementation uses uint64_t for the integer calculations.
 *
 * Sets up Vose alias sampling tables, see
 *   https://www.keithschwarz.com/darts-dice-coins/
 *
 * Copyright (c) Asbjørn M. Bonvik 2025.
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

#include "cmi_calc.h"

#define ARRSIZE 256
double xarr[ARRSIZE] = { 0.0 };
double yarr[ARRSIZE] = { 0.0 };
double concavity[ARRSIZE] = { 0.0 };
double area[ARRSIZE] = { 0.0 };
double prob[ARRSIZE] = { 0.0 };
double x_tail = 0.0;
uint64_t uprob[ARRSIZE] = { 0 };
uint8_t alias[ARRSIZE] = { 0 };
uint8_t i_max = 0;

double pdf(const double x) {
    return exp(-x);
}

double cdf(const double x) {
    return 1 - exp(-x);
}

static void calculate_ziggurat(void) {
    int last = 0;
    double xlcand = 1.0;
    double xrcand = 10.0;
    double yprev = 0.0;
    double acum = 0.0;

    /* Fit in as many equal-sized rectangles as possible */
    for (int i = 0; i < ARRSIZE; i++) {
        struct layer cand;
        cand.tgt_area = 1.0 / ARRSIZE;
        cand.x0 = 0.0;
        cand.y0 = yarr[i - 1];

        /* search for the next layer upper-right corner, ensuring that the candidate solutions bracket a root */
        double xmid;
        if ((layer_error(xlcand, &cand) * layer_error(xrcand, &cand) < 0.0)
            && cmi_bisection(xlcand, xrcand, layer_error, &cand, &xmid)) {
            /* Found a corner point, note it down */
            xarr[i] = xmid;
            yarr[i] = pdf(xmid);

            /* Calculate and store the area to the right of the rectangle, between it and the pdf curve */
            if (i == 0) {
                /* First layer, use area of tail */
                area[i] = 1.0 - cdf(xmid);
                x_tail = xmid;
            }
            else {
                area[i] = (cdf(xarr[i - 1]) - cdf(xarr[i])) - (xarr[i - 1] - xarr[i]) * yarr[i - 1];
                const double xargmax = log(-(xarr[i - 1] - xarr[i]) / (yarr[i - 1] - yarr[i]));
                const double ypdf = pdf(xargmax);
                const double yline = (xargmax - xarr[i]) * (yarr[i - 1] - yarr[i]) / (xarr[i - 1] - xarr[i]) + yarr[i];
                concavity[i] = yline - ypdf;
            }
            acum += area[i] + (xarr[i] - 0.0) * (yarr[i] - yprev);

            /* Make ready for the next layer */
            yprev = yarr[i];
            xlcand = xmid / 2.5;
            xrcand = xmid;
            last = i;
        }
        else {
            /* Special handling for the top area, conceptually to the right of a zero-width one */
            i_max = last;
            uint8_t top = last + 1;
            xarr[top] = 0.0;
            yarr[top] = 1.0;
            area[top] = 1.0 - acum;

            const double xargmax = log(-(xarr[top - 1] - xarr[top])
                             / (yarr[top - 1] - yarr[top]));
            const double ypdf = pdf(xargmax);
            const double yline = (xargmax - xarr[top]) * (yarr[top - 1] - yarr[top])
                           / (xarr[top - 1] - xarr[top]) + yarr[top];
            concavity[top] = yline - ypdf;
        }
    }
}

static void calculate_alias_table(void) {
    double asum = 0.0;
    for (int i = 0; i < ARRSIZE; i++)
        asum += area[i];

    double work[ARRSIZE] = { 0.0 };
    uint8_t small[ARRSIZE] = { 0 };
    uint8_t large[ARRSIZE] = { 0 };
    uint8_t idxs = 0;
    uint8_t idxl = 0;
    for (int i = 0; i < ARRSIZE; i++) {
        work[i] = area[i] * ARRSIZE  / asum;
        if (work[i] < 1.0) {
            small[idxs++] = i;
        }
        else {
            large[idxl++] = i;
        }
    }

    while ((idxs > 0) && (idxl > 0)) {
        uint8_t l = small[--idxs];
        uint8_t g = large[--idxl];
        prob[l] = work[l];
        assert(prob[l] <= 1.0);
        alias[l] = g;
        work[g] = (work[g] + work[l]) - 1.0;
        if (work[g] < 1.0 ) {
            small[idxs++] = g;
        }
        else {
            large[idxl++] = g;
        }
    }

    while (idxl > 0) {
        uint8_t g = large[--idxl];
        prob[g] = 1.0;
    }

    while (idxs > 0) {
        uint8_t l = small[--idxs];
        prob[l] = 1.0;
    }

    for (int i = 0; i < ARRSIZE; i++) {
        assert(prob[i] <= 1.0);
        if (prob[i] == 1.0) {
            /* May accidentally round upwards and overflow in conversion to double */
            uprob[i] = UINT64_MAX;
        }
        else {
            /* Safe, may round upwards to 2^64 in conversion, but will then multiply by something < 1 */
            uprob[i] = (uint64_t)(prob[i] * (double)UINT64_MAX);
        }
    }
}

static void print_c_code(void) {
    /* We have all we need, now write the C code to be #included in the actual code */
    printf("/*\n");
    printf(" * cmi_random_exp_zig.inc - local file to be included in cmb_random.c,\n");
    printf(" * hiding the lookup table from view in main code\n");
    printf(" */\n");

    printf("\n/* Index of top layer in ziggurat, each layer with probability 1/256 */\n");
    printf("const uint8_t cmi_random_exp_zig_max = %d;\n", i_max);

    printf("\n/* Ziggurat corner points (X, Y) on the pdf curve, scaled by 2^-64 */\n");
    printf("const double cmi_random_exp_zig_pdf_x[%d] = {",ARRSIZE);
    for (int i = 0; i < ARRSIZE-1; i++) {
        printf(" %.15g,", ldexp(xarr[i], -64));
    }
    printf(" %.15g };\n", ldexp(xarr[ARRSIZE-1], -64));

    printf("static const double cmi_random_exp_zig_pdf_y[%d] = {",ARRSIZE);
    for (int i = 0; i < ARRSIZE-1; i++) {
        printf(" %.15g,", ldexp(yarr[i], -64));
    }
    printf(" %.15g };\n", ldexp(yarr[ARRSIZE-1], -64));

    printf("\n/* Max distance from linear interpolation to actual pdf in each overhang, scaled to uint64_t */\n");
    printf("static const uint64_t exp_zig_u_concavity[%d] = { 0x%016llxull", ARRSIZE, 0ull);
    for (int i = 1; i <= i_max + 1; i++) {
        uint64_t uconcavity = (uint64_t) ((double) UINT64_MAX * (concavity[i] / (yarr[i] - yarr[i - 1])));
        printf(", 0x%016llxull", uconcavity);
    }
    printf(" };\n");;

    printf("\n/* Alias table, probabilities scaled to uint64_t */\n");
    printf("static const uint8_t exp_zig_alias[%d] = {",ARRSIZE);
    for (int i = 0; i < ARRSIZE-1; i++) {
        printf(" %d,", alias[i]);
    }
    printf(" %d };\n", alias[ARRSIZE-1]);

    printf("static const uint64_t exp_zig_u_prob[%d] = {",ARRSIZE);
    for (int i = 0; i < ARRSIZE-1; i++) {
        printf(" 0x%016llxull,", uprob[i]);
    }
    printf(" 0x%016llxull };\n", uprob[ARRSIZE-1]);

    printf("\n/* Actual X value for the beginning of the tail */\n");
    printf("static const double exp_zig_x_tail_start = %.15g;\n", x_tail);
}

int main(void) {
    calculate_ziggurat();
    calculate_alias_table();
    print_c_code();

    return 0;
}
