/*
 * calc_normal.c - program to calculate the ziggurat lookup tables for
 * standard normal distribution, see
 *   https://en.wikipedia.org/wiki/Ziggurat_algorithm#McFarland's_variation
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
double convexity[ARRSIZE] = { 0.0 };
double area[ARRSIZE] = { 0.0 };
double prob[ARRSIZE] = { 0.0 };
double x_tail = 0.0;
int64_t iprob[ARRSIZE] = { 0 };
uint8_t alias[ARRSIZE] = { 0 };
uint8_t i_max = 0;
uint8_t i_inflection = 0;

double pdf(const double x)
{
    return exp(-0.5 * x * x) / sqrt(2.0 * M_PI);
}

double cdf(const double x)
{
    return 0.5 * (1.0 + erf(x / sqrt(2.0)));
}

static void calculate_ziggurat(void)
{
    int last = 0;
    double xlcand = 3.0;
    double xrcand = 4.0;
    double acum = 0.0;

    /* Fit in as many equal-sized rectangles as possible */
    for (int i = 0; i < ARRSIZE; i++) {
        struct layer cand;
        cand.tgt_area = 0.5 / ARRSIZE;
        cand.x0 = 0.0;
        cand.y0 = yarr[i-1];

        double xmid;
        /* search for the next layer upper-right corner, ensuring that the candidate solutions bracket a root */
        if ((layer_error(xlcand, &cand) * layer_error(xrcand, &cand) < 0.0)
              && cmi_bisection(xlcand, xrcand, layer_error, &cand, &xmid)) {
            /* Found a corner point, note it down */
            xarr[i] = xmid;
            yarr[i] = pdf(xmid);

            /* Calculate and store the area to the right of the rectangle, between it and the pdf curve */
            if (i == 0) {
                /* First layer, use area of tail */
                area[i] = 1.0 - cdf(xarr[i]);
                x_tail = xarr[i];
             }
            else {
                area[i] = (cdf(xarr[i-1]) - cdf(xarr[i])) - (xarr[i-1] - xarr[i]) * yarr[i-1];
                 /* Find points of max concavity or convexity by finding zeroes of the error function derivative.
                 * Three cases: Below inflection point (convex), above inflection point (concave),
                 * or straddling inflection point (both, either side of x = 1.0)
                 */
                double xargmax;
                struct segment seg;
                seg.x1 = xarr[i];
                seg.y1 = yarr[i];
                seg.x2 = xarr[i-1];
                seg.y2 = yarr[i-1];

                if (xarr[i] > 1.0) {
                    /* concave region */
                    (void)cmi_bisection(xarr[i], xarr[i-1], dist_deriv, &seg, &xargmax);
                    double ypdf = pdf(xargmax);
                    double yline = linear_int(xargmax, &seg);
                    concavity[i] = yline - ypdf;
                }
                else if (xarr[i-1] < 1.0) {
                    /* convex region */
                    (void)cmi_bisection(xarr[i], xarr[i-1], dist_deriv, &seg,  &xargmax);
                    double ypdf = pdf(xargmax);
                    double yline = linear_int(xargmax, &seg);
                    convexity[i] = ypdf - yline;
                }
                else {
                    /* straddling inflection point, convex to the left, concave to the right */
                    assert((xarr[i] < 1.0) && (xarr[i-1] > 1.0));
                    i_inflection = i;

                    (void)cmi_bisection(xarr[i], 1.0, dist_deriv, &seg,  &xargmax);
                    double ypdf = pdf(xargmax);
                    double yline = linear_int(xargmax, &seg);
                    convexity[i] = ypdf - yline;

                    (void)cmi_bisection(1.0, xarr[i-1], dist_deriv, &seg,  &xargmax);
                    ypdf = pdf(xargmax);
                    yline = linear_int(xargmax, &seg);
                    concavity[i] = yline - ypdf;
                }
            }

            acum += area[i] + (xarr[i] - 0.0) * (yarr[i] - yarr[i-1]);

            /* Make ready for the next layer */
            xlcand = xarr[i] / 1.2;
            xrcand = xarr[i];
            last = i;
        }
        else {
            /* no more points to find, apparently */
            i_max = last;
            uint8_t top = last + 1;
            xarr[top] = 0.0;
            yarr[top] = pdf(0.0);
            area[top] = 0.5 - acum;

            double xargmax;
            struct segment seg;
            seg.x1 = xarr[top];
            seg.y1 = yarr[top];
            seg.x2 = xarr[top-1];
            seg.y2 = yarr[top-1];

            (void)cmi_bisection(xarr[top], xarr[top-1], dist_deriv, &seg,  &xargmax);
            const double ypdf = pdf(xargmax);
            const double yline = (xargmax - xarr[top]) * (yarr[top-1] - yarr[top]) / (xarr[top-1] - xarr[top]) + yarr[top];
            assert(ypdf > yline);
            convexity[top] = ypdf - yline;
            break;
        }
    }
}

static void calculate_alias_table(void)
{
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
            iprob[i] = INT64_MAX;
        }
        else {
            /* Safe, may round upwards to 2^64 in conversion, but will then multiply by something < 1 */
            iprob[i] = (int64_t)(prob[i] * (double)INT64_MAX);
        }
    }
}

static void print_c_code(void)
{
    /* We have all we need, now write the C code to be #included in the actual code */
    printf("/*\n");
    printf(" * cmi_random_nor_zig.inc - local file to be included in cmb_random.c,\n");
    printf(" * hiding the lookup table from view in main code\n");
    printf(" */\n");

    printf("\n/* Index of top layer in ziggurat, each layer with probability 1/256 */\n");
    printf("const uint8_t cmi_random_nor_zig_max = %d;\n", i_max);

    printf("\n/* Ziggurat corner points (X, Y) on the pdf curve, x-axis scaled by 2^-63 */\n");
    printf("const double cmi_random_nor_zig_pdf_x[%d] = {",ARRSIZE);
    for (int i = 0; i < ARRSIZE-1; i++) {
        printf(" %.15g,", ldexp(xarr[i], -63));
    }
    printf(" %.15g };\n", ldexp(xarr[ARRSIZE-1], -63));

    printf("\n/* y-axis scaled by sqrt(2.0 * M_PI) to avoid recomputing the constant at runtime */\n");
    printf("static const double cmi_random_nor_zig_pdf_y[%d] = {",ARRSIZE);
    for (int i = 0; i < ARRSIZE-1; i++) {
        printf(" %.15g,", ldexp(yarr[i] * sqrt(2.0 * M_PI), -63));
    }
    printf(" %.15g };\n", ldexp(yarr[ARRSIZE-1], -63));

    int64_t max_iconcavity = 0ll;
    printf("\n/* Max distance from linear interpolation to actual pdf in each overhang, scaled to int64_t */\n");
    printf("static const int64_t nor_zig_i_concavity[%d] = { 0x%016llxll", ARRSIZE, 0ll);
    for (int i = 1; i <= i_max + 1; i++) {
        int64_t iconcavity = (int64_t) ((double) INT64_MAX * ((concavity[i] * sqrt(2.0 * M_PI)) / (yarr[i] - yarr[i - 1])));
        printf(", 0x%016llxll", iconcavity);
        if (iconcavity > max_iconcavity) {
            max_iconcavity = iconcavity;
        }
    }
    printf(" };\n");

    int64_t max_iconvexity = 0ll;
    printf("static const int64_t nor_zig_i_convexity[%d] = { 0x%016llxll", ARRSIZE, 0ll);
    for (int i = 1; i <= i_max + 1; i++) {
        int64_t iconvexity = (int64_t) ((double) INT64_MAX * ((convexity[i] * sqrt(2.0 * M_PI)) / (yarr[i] - yarr[i - 1])));
        printf(", 0x%016llxll", iconvexity);
        if (iconvexity > max_iconvexity) {
            max_iconvexity = iconvexity;
        }
    }
    printf(" };\n");

    printf("\n/* Alias table, probabilities scaled to int64_t */\n");
    printf("static const uint8_t nor_zig_alias[%d] = {",ARRSIZE);
    for (int i = 0; i < ARRSIZE-1; i++) {
        printf(" %d,", alias[i]);
    }
    printf(" %d };\n", alias[ARRSIZE-1]);

    printf("static const int64_t nor_zig_i_prob[%d] = {",ARRSIZE);
    for (int i = 0; i < ARRSIZE-1; i++) {
        printf(" 0x%016llxll,", iprob[i]);
    }
    printf(" 0x%016llxll };\n", iprob[ARRSIZE-1]);

    printf("\n/* Layer where the inflection point occurs */\n");
    printf("static const uint8_t nor_zig_inflection = %d;\n", i_inflection);
    printf("\n/* Actual X value for the beginning of the tail */\n");
    printf("static const double nor_zig_x_tail_start = %.15g;\n", x_tail);
    printf("static const double nor_zig_inv_tail_start = %.15g;\n", 1.0 / x_tail);
    printf("\n/* Maximal concavity and convexity value */\n");
    printf("static const int64_t nor_zig_max_i_concavity = 0x%016llxll;\n", max_iconcavity);
    printf("static const int64_t nor_zig_max_i_convexity = 0x%016llxll;\n", max_iconvexity);
}

int main(void)
{
    calculate_ziggurat();
    calculate_alias_table();
    print_c_code();

    return 0;
}
