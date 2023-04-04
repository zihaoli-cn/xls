#!/bin/bash

# 指定数据目录和目标目录
executable="/home/xls-developer/tmp/xls/bazel-bin/xls/p5/tools/convert_ir_main"
data_dir="/home/xls-developer/tmp/xls/xls/p5/data/generated"
target_dir="/home/xls-developer/tmp/xls/xls/p5/data/benchmark"

# 遍历数据目录下的所有 JSON 文件
for file in "$data_dir"/*.json; do
  # 如果文件是普通文件而不是目录
  if [ -f "$file" ]; then
    # 执行可执行程序，并将当前文件作为参数传入
    "$executable" "$file"
    # 判断程序是否出错
    if [ $? -eq 0 ]; then
      # 如果程序没有出错，将当前文件复制到目标目录下
      cp "$file" "$target_dir"
    fi
  fi
done
