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

inline double layer_error(double x, void *vp) {
    struct layer *lp = vp;
    return (x - lp->x0) * (pdf(x) - lp->y0) - lp->tgt_area;
}

struct segment {
    double x1, y1;
    double x2, y2;
};

inline double linear_int(double x, struct segment *sp) {
    return sp->y1 + (x - sp->x1) * (sp->y2 - sp->y1) / (sp->x2 - sp->x1);
}

inline double segment_error(double x, void *vp) {
    struct segment *sp = vp;
    return linear_int(x, sp) - pdf(x);
}

inline double dist_deriv(double x, void *vp) {
    struct segment *sp = vp;
    double a = (sp->y2 - sp->y1)/(sp->x2 - sp->x1);
    return -x * pdf(x) - a;
}

inline int bisection(double x_left, double x_right, double (*f)(double x, void *vp), void *vp, double *x_root) {
    static const double max_eps = 1e-15;
    static const unsigned max_iter = 1000000;

    /* Initial guesses must bracket the root, i.e. have opposite signs */
    double y_left = f(x_left, vp);
    double y_right = f(x_right, vp);
    assert((y_left * y_right) <= 0.0);

    int i = 0;
    do {
        double x_mid = (x_left + x_right) * 0.5;
        double y_mid = f(x_mid, vp);
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

#endif
