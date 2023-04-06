#!/bin/bash

# 指定数据目录和目标目录
data_dir="/home/xls-developer/tmp/xls/xls/p5/data/benchmark"

generator="/home/xls-developer/tmp/xls/bazel-bin/xls/p5/tools/ast_gen_mod_main"
validator="/home/xls-developer/tmp/xls/bazel-bin/xls/p5/tools/convert_ir_main"
source_dir="/home/xls-developer/tmp/xls/xls/p5/data/benchmark"


declare -i iteraion=6

while ((i <= 100))
do
    ((sum += i))
    ((i++))
done


# 遍历数据目录下的所有 JSON 文件
for file in "$source_dir"/*.json; do
    # 如果文件是普通文件而不是目录
    declare -i counter=0

    #while ((counter < 4))
    #do 
        if [ -f "$file" ]; then
            # 执行可执行程序，并将当前文件作为参数传入
            "$generator" "$file"
            # 判断程序是否出错
            if [ $? -eq 0 ]; then
                if [ -f "out0.json" ]; then
                    "$validator" "out0.json"
                    if [ $? -eq 0 ]; then
                        cp "out0.json" "$data_dir/iter$iteraion-$counter.json"
                        counter=$(expr $counter + 1)
                    fi
                fi

                if [ -f "out1.json" ]; then
                    "$validator" "out1.json"
                    if [ $? -eq 0 ]; then
                        cp "out1.json" "$data_dir/iter$iteraion-$counter.json"
                        counter=$(expr $counter + 1)
                    fi
                fi

                if [ -f "out2.json" ]; then
                    "$validator" "out2.json"
                    if [ $? -eq 0 ]; then
                        cp "out2.json" "$data_dir/iter$iteraion-$counter.json"
                        counter=$(expr $counter + 1)
                    fi
                fi

                if [ -f "out3.json" ]; then
                    "$validator" "out3.json"
                    if [ $? -eq 0 ]; then
                        cp "out3.json" "$data_dir/iter$iteraion-$counter.json"
                        counter=$(expr $counter + 1)
                    fi
                fi
            fi
        fi
    #done

    iteraion=$(expr $iteraion + 1)
done
