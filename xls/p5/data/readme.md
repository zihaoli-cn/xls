# 数据文件说明
1. v1文件夹下，给出的是最早版本的数据文件，包含了完整的数据，包括各种数据类型的声明，然后还有HEADER、TABLE、ACTION等等，文件夹下的PPT文件给出了完整的数据说明
2. v2文件夹下，给出的是去掉了所有声明的数据文件，整个json文件就是一个的BLOCK类型，就是ACTION里面的函数体，而且FOR这些都给展开了，除了built-in function，都给展开了
3. v3文件夹下，在v2的基础上增加了struct field access类型信息、全局变量是否public
4. v4文件夹下，在v4的基础上增加了CAST类型的标注，标注了哪些IDEN是enum，能够用来区分是否是一个常数
5. example.json文件，是一个自己手写的json AST，用来简单验证正确性
