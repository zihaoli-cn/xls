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
    n_lines = 0
    for line in f2.readlines():
        entries = line.split(',')
        if len(entries) < 2:
            continue
        n_lines += 1
        constraint_time.append(float(entries[0]))
        solver_time.append(float(entries[1]))
        constraints.append(int(entries[2]))
        clock_related_constraints.append(int(entries[3]))

    assert(len(constraint_time) == len(solver_time))
    assert((n_lines % 2) == 0)
    
    counter = 0
    for sample in samples:
        for data in sample["data"]:
            data["t_sdc_solver"] = solver_time[counter]
            data["t_sdc_solver2"] = solver_time[counter+1]
            data["constraints_computing_time"] = constraint_time[counter]
            data["constraints_computing_time2"] = constraint_time[counter+1]
            data["constraints"] = constraints[counter]
            data["constraints2"] = constraints[counter+1]
            data["clock_related_constraints"] = clock_related_constraints[counter]
            data["clock_related_constraints2"] = clock_related_constraints[counter+1]
            counter += 2

    print(json.dumps(samples, indent = 2))
