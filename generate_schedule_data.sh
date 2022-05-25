#--copt="-DENABLE_LOG_TO_CERR" 
bazel build -c opt --copt="-DLOG_SDC_INTERNAL_RUNNING_TIME_TO_CERR" //xls/scheduling:benchmark
bazel-bin/xls/scheduling/benchmark >/tmp/schedule_benchmark.json 2>/tmp/internal_timing.txt
python3 merge_data.py /tmp/schedule_benchmark.json /tmp/internal_timing.txt > result.json