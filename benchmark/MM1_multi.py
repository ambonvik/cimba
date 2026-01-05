import simpy
import random
import multiprocessing
import statistics
import math

NUM_OBJECTS = 1000000
ARRIVAL_RATE = 0.9
SERVICE_RATE = 1.0
NUM_TRIALS = 100

def arrival_process(env, store, n_limit, arrival_rate):
    for _ in range(n_limit):
        t = random.expovariate(arrival_rate)
        yield env.timeout(t)
        yield store.put(env.now)

def service_process(env, store, service_rate, stats):
    while True:
        arr_time = yield store.get()
        t = random.expovariate(service_rate)
        yield env.timeout(t)
        stats['sum_tsys'] += env.now - arr_time
        stats['obj_cnt'] += 1

def run_trial(args):
    n_objects, arr_rate, svc_rate = args
    env = simpy.Environment()
    store = simpy.Store(env)
    stats = {'sum_tsys': 0.0, 'obj_cnt': 0}
    env.process(arrival_process(env, store, n_objects, arr_rate))
    env.process(service_process(env, store, svc_rate, stats))
    env.run()

    return stats['sum_tsys'] / stats['obj_cnt']

def main():
    num_cores = multiprocessing.cpu_count()
    trl_args = [(NUM_OBJECTS, ARRIVAL_RATE, SERVICE_RATE)] * NUM_TRIALS
    with multiprocessing.Pool(processes=num_cores) as pool:
        results = pool.map(run_trial, trl_args)

    valid_results = [r for r in results if r > 0]
    n = len(valid_results)
    if (n > 1):
        mean_tsys = statistics.mean(valid_results)
        sdev_tsys = statistics.stdev(valid_results)
        serr_tsys = sdev_tsys / math.sqrt(n)
        ci_w = 1.96 * serr_tsys
        ci_l = mean_tsys - ci_w
        ci_u = mean_tsys + ci_w

        print(f"Average time in system: {mean_tsys} (n {n}, conf int {ci_l} - "
              f"{ci_u}, expected: "
              f"{1.0 / (SERVICE_RATE - ARRIVAL_RATE)})")

if __name__ == "__main__":
    main()
