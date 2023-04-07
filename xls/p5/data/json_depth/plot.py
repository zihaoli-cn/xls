import matplotlib.pyplot as plt

input_files=["series2.txt", "series3.txt", "series4.txt"]


def plot(input_files):
    # 从txt文件中读取数据
    data=[]
    for input_file in input_files:
        with open(input_file, 'r') as f:
            data.append(f.read().strip().split(','))

    # 将字符串数据转换为整数列表
    data = [[int(x) for x in line] for line in data]

    # 绘制时间序列折线图
    plt.figure()

    plt.subplot(311)
    plt.plot(data[0], linewidth=0.55)
    plt.ylabel('Depth')

    plt.subplot(312)
    plt.plot(data[1], linewidth=0.55)
    plt.ylabel('Depth')

    plt.subplot(313)
    plt.plot(data[2], linewidth=0.55)

    # 添加标题和坐标轴标签
    plt.xlabel('Visited elements')
    plt.ylabel('Depth')

    # 保存图片为jpeg格式
    plt.tight_layout()
    plt.savefig('series.png', dpi=400)
    


plot(input_files)