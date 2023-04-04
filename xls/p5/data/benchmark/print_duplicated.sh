#!/bin/bash

# 定义一个数组来存储所有的json文件名
declare -a json_files=($(find . -name "*.json"))

# 遍历所有的json文件，使用 diff 命令比较每一对文件
for ((i=0; i<${#json_files[@]}-1; i++))
do
    for ((j=i+1; j<${#json_files[@]}; j++))
    do
        if diff "${json_files[i]}" "${json_files[j]}" > /dev/null
        then
            echo "相同的文件对：${json_files[i]} 和 ${json_files[j]}"
            rm "${json_files[j]}"
        fi
    done
done
