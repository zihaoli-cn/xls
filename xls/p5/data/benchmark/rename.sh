#!/bin/bash

# 定义一个数组来存储所有的json文件名
declare -a json_files=($(find . -name "*.json"))

# 遍历所有的json文件，使用 diff 命令比较每一对文件
for ((i=0; i<${#json_files[@]}; i++))
do
    if [ -f "bench$i.json" ]; then
        echo "file bench$i.json exists"
    else
        mv "${json_files[i]}" "bench$i.json"
    fi
    
done
