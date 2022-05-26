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
    constraints = []
    clock_related_constraints = []
    for line in f2.readlines():
        entries = line.split(',')
        if len(entries) < 2:
            continue
        constraint_time.append(float(entries[0]))
        solver_time.append(float(entries[1]))
        constraints.append(int(entries[2]))
        clock_related_constraints.append(int(entries[3]))

    assert(len(constraint_time) == len(solver_time))
    
    counter = 0
    for sample in samples:
        for data in sample["data"]:
            data["t_sdc_solver"] = solver_time[counter]
            data["constraints_computing_time"] = constraint_time[counter]
            data["constraints"] = constraints[counter]
            data["clock_related_constraints"] = clock_related_constraints[counter]

            counter += 1

    print(json.dumps(samples, indent = 2))
