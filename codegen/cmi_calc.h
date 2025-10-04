/*
 * cmi_calc.h - utility functions for calculating ziggurat lookup tables.
 *
 * Copyright (c) Asbjørn M. Bonvik 1994, 1995, 2025.
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

#ifndef CMI_CALC_H
#define CMI_CALC_H
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323
#endif

extern double pdf(double x);
extern double cdf(double x);

struct layer {
    double x0, y0;
    double tgt_area;
};

inline double layer_error(double x, void *vp)
{
    struct layer *lp = vp;
    return (x - lp->x0) * (pdf(x) - lp->y0) - lp->tgt_area;
}

struct segment {
    double x1, y1;
    double x2, y2;
};

inline double linear_int(double x, struct segment *sp)
{
    return sp->y1 + (x - sp->x1) * (sp->y2 - sp->y1) / (sp->x2 - sp->x1);
}

inline double segment_error(double x, void *vp)
{
    struct segment *sp = vp;
    return linear_int(x, sp) - pdf(x);
}

inline double dist_deriv(double x, void *vp)
{
    struct segment *sp = vp;
    double a = (sp->y2 - sp->y1)/(sp->x2 - sp->x1);
    return -x * pdf(x) - a;
}

extern int cmi_bisection(double x_left,
                         double x_right,
                         double (*f)(double x, void *vp),
                         void *vp,
                         double *x_root);

#endif
