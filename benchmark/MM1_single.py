import simpy
import random
import time
import multiprocessing
import statistics

NUM_OBJECTS = 1000000
ARRIVAL_RATE = 0.9
SERVICE_RATE = 1.0

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

def run_trial():
    env = simpy.Environment()
    store = simpy.Store(env)
    stats = {'sum_tsys': 0.0, 'obj_cnt': 0}
    env.process(arrival_process(env, store, NUM_OBJECTS, ARRIVAL_RATE))
    env.process(service_process(env, store, SERVICE_RATE, stats))
    env.run()

    return stats['sum_tsys'] / stats['obj_cnt']

def main():
    avg_tsys = run_trial()
    print(f"Average time in system: {avg_tsys}, expected: "
          f"{1.0 / (SERVICE_RATE - ARRIVAL_RATE)}")

if __name__ == "__main__":
    main()

