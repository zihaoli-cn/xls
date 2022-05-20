bazel build -c opt --copt="-DLOG_SDC_INTERNAL_RUNNING_TIME_TO_CERR" //xls/scheduling:benchmark
bazel-bin/xls/scheduling/benchmark >schedule_benchmark.json 2> internal_timing.txt