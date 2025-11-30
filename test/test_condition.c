/*
 * Test script for condition variables. Creates a complex mixed state simulation
 * of a harbor, where tides and wind conditions are state variables. Tugs and
 * berths are modelled as resource stores. Arriving ships come in various sizes,
 * with different resource needs and different requirements to max wind and min
 * water depth. The entire package of states and resources is modelled as a
 * condition variable that the ship processes can wait for before docking. The
 * time unit is one hour.
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
#include <stdint.h>
#include <time.h>

#include "cimba.h"

#include "test.h"

#define USERFLAG 0x00000001

struct simulation {
    struct cmb_process *weather;
    struct cmb_process *tide;
    struct cmb_process *arrivals;
    struct cmb_list *active_ships;
    struct cmb_resourcestore *tugs;
    struct cmb_resourcestore *small_berths;
    struct cmb_resourcestore *large_berths;
    struct cmb_condition *harbormaster;
};

struct state {
    double wind_magnitude;
    double wind_direction;
    double water_depth;
};

static void end_sim_evt(void *subject, void *object)
{
    cmb_assert_debug(subject != NULL);
    cmb_unused(object);

    const struct simulation *sim = subject;
    cmb_process_stop(sim->weather, NULL);
    cmb_process_stop(sim->tide, NULL);
}

/* A process that updates the weather once per hour */
void *weather_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    while (true) {
        struct state *state = vctx;
        const double wmag = cmb_random_rayleigh(5.0);
        double wold = state->wind_magnitude;
        state->wind_magnitude = 0.5 * wmag + 0.5 * wold;

        const double wdir1 = cmb_random_PERT(0.0, 225.0, 360.0);
        const double wdir2 = cmb_random_PERT(0.0,  45.0, 360.0);
        state->wind_direction = 0.75 * wdir1 + 0.25 * wdir2;

        cmb_logger_user(stdout, USERFLAG,
                        "Wind: %5.1f m/s %03.0f deg",
                        state->wind_magnitude,
                        state->wind_direction);

        cmb_process_hold(1.0);
    }
}

/* A process that updates the water depth once per hour */
void *tide_proc(struct cmb_process *me, void *vctx)
{
    cmb_unused(me);
    cmb_assert_debug(vctx != NULL);

    while (true) {
        struct state *state = vctx;
        const double t = cmb_time();
        const double da0 = 15.0;
        const double da1 = 1.0 * sin(2.0 * M_PI * t / 12.4);
        const double da2 = 0.5 * sin(2.0 * M_PI * t / 24.0);
        const double da3 = 0.25 * sin(2.0 * M_PI * t / (0.5 * 29.5 * 24));
        const double da = da0 + da1 + da2 + da3;

        const double dw = 0.5 * state->wind_magnitude
                         * sin(state->wind_direction * M_PI / 180.0);

        state->water_depth = da - dw;
        cmb_logger_user(stdout, USERFLAG,
                        "Water: %5.1f m",
                        state->water_depth);

        cmb_process_hold(1.0);
    }
}

void test_condition(void)
{
    const uint64_t seed = cmb_random_get_hwseed();
    printf("seed: %llu\n", seed);
    cmb_random_initialize(seed);
    cmb_event_queue_initialize(0.0);

    struct state state = { 0.0, 0.0, 0.0 };

    printf("Create weather and tide process\n");
    struct simulation sim;
    sim.weather = cmb_process_create();
    cmb_process_initialize(sim.weather, "Weather", weather_proc, &state, 0);
    cmb_process_start(sim.weather);
    sim.tide = cmb_process_create();
    cmb_process_initialize(sim.tide, "Depth", tide_proc, &state, 0);
    cmb_process_start(sim.tide);

    printf("Schedule end event\n");
    (void)cmb_event_schedule(end_sim_evt, &sim, NULL, 100.0, 0);

    printf("Execute simulation\n");
    cmb_logger_flags_off(CMB_LOGGER_INFO);
    cmb_event_queue_execute();

    printf("Clean up\n");
    cmb_process_terminate(sim.weather);
    cmb_process_destroy(sim.weather);
    cmb_process_terminate(sim.tide);
    cmb_process_destroy(sim.tide);
    cmb_event_queue_terminate();
}


int main(void)
{
    cmi_test_print_line("*");
    printf("****************************   Testing condition   *****************************\n");
    cmi_test_print_line("*");

    test_condition();

    cmi_test_print_line("*");
    return 0;
}