# 关于此次重构的想法
1. 关于接口是否需要使用class
   1. 如果不持有数据，尽量使用C-like API
   2. 只有需要一定时间内持有数据的，才使用class
2. 把可执行文件单独放在tools下面
3. ast的replace child这个接口有点不行，需要改改
4. lowering那一部分，最好提供一个dfs-visitor，然后把每一个lowering的transform套在dfs-visitor上