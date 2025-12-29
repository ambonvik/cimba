#include <stdio.h>
#include <cimba.h>


void write_gnuplot_commands(void)
{
    FILE *cmdfp = fopen("random_crossplot.gp", "w");

    fprintf(cmdfp, "set terminal qt size 1200,1200 enhanced font 'Arial,12'\n");
    fprintf(cmdfp, "set title \"Cross-plotting successive samples\" "
                   "font \"Times Bold, 18\" \n");
    fprintf(cmdfp, "set grid\n");
    fprintf(cmdfp, "set xlabel \"x\"\n");
    fprintf(cmdfp, "set ylabel \"y\"\n");
    fprintf(cmdfp, "set xrange [0.0:1.0]\n");
    fprintf(cmdfp, "set yrange [0.0:1.0]\n");
    fprintf(cmdfp, "datafile = 'random_crossplot.dat'\n");
    fprintf(cmdfp, "plot datafile with dots\n");

    fclose(cmdfp);
}

int main(void)
{
    const unsigned n = 1000000;

    cmb_random_initialize(cmb_random_get_hwseed());
    FILE *fp = fopen("random_crossplot.dat", "w");
    for (unsigned ui = 0; ui < n; ui++) {
        const double x = cmb_random();
        const double y = cmb_random();
        fprintf(fp, "%f\t%f\n", x, y);
    }

    fclose(fp);
    write_gnuplot_commands();
    system("gnuplot -persistent random_crossplot.gp");
}