import json
import sys

if __name__ == "__main__":
    json_filename = sys.argv[1]
    f1 = open(json_filename, "r")
    samples = json.load(f1)

    timing_filename = sys.argv[2]
    f2 = open(timing_filename, "r")

    constraint_time = []
    solver_time = []
    num_constraint = []
    for line in f2.readlines():
        times = line.split(',')
        if len(times) < 2:
            continue
        constraint_time.append(float(times[0]))
        solver_time.append(float(times[1]))
        num_constraint.append(float(times[2]))

    assert(len(constraint_time) == len(solver_time))
    
    counter = 0
    for sample in samples:
        for data in sample["data"]:
            data["solver_running_time"] = solver_time[counter]
            data["constraints_computing_time"] = constraint_time[counter]
            data["num_constraint"] = num_constraint[counter]
            counter += 1

    print(json.dumps(samples, indent = 2))
