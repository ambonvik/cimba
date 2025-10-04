/*
* cmi_calc_utils.h - utility functions for calculating ziggurat lookup tables.
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

#include <stdio.h>

#include "cmi_calc.h"

int cmi_bisection(double x_left,
                  double x_right,
                  double (*f)(double x, void *vp),
                  void *vp,
                  double *x_root)
{
    static const double max_eps = 1e-15;
    static const unsigned max_iter = 1000000u;

    /* Initial guesses must bracket the root, i.e. have opposite signs */
    double y_left = f(x_left, vp);
    double y_right = f(x_right, vp);
    assert((y_left * y_right) <= 0.0);

    unsigned int i = 0;
    do {
        const double x_mid = (x_left + x_right) * 0.5;
        const double y_mid = f(x_mid, vp);
        if (fabs(y_mid) < max_eps) {
            *x_root = x_mid;
            return 1;
        }
        else if (y_mid * y_left > 0.0) {
            x_left = x_mid;
            y_left = f(x_left, vp);
        }
        else {
            assert(y_mid * y_right >= 0.0);
            x_right = x_mid;
            y_right = f(x_right, vp);
        }
    } while (++i < max_iter);

    fprintf(stderr, "fell out of bisection, current xl %g yl %g xr %g yr %g\n", x_left, y_left, x_right, y_right);
    return 0;
}
