import numpy as np
import matplotlib.pyplot as plt

# 从txt文件中读取x轴和y轴数据
with open('data.txt', 'r') as f:
    x = f.readline().strip().split(',')
    y1 = f.readline().strip().split(',')
    y2 = f.readline().strip().split(',')
    y3 = f.readline().strip().split(',')
    y4 = f.readline().strip().split(',')

# 将字符串数据转换为浮点数列表
x = [float(xi) for xi in x]
y1 = [float(yi) for yi in y1]
y2 = [float(yi) for yi in y2]
y3 = [float(yi) for yi in y3]
y4 = [float(yi) for yi in y4]

# 绘制散点图
plt.scatter(x, y1, color='blue')
plt.scatter(x, y2, color='red')
plt.scatter(x, y3, color='green')
plt.scatter(x, y4, color='black')

# 计算最小二乘法拟合直线的斜率和截距
fit1 = np.polyfit(x, y1, 1)
fit2 = np.polyfit(x, y2, 1)
fit3 = np.polyfit(x, y3, 1)
fit4 = np.polyfit(x, y4, 1)

x = np.array(x)

# 绘制最小二乘法拟合直线
plt.plot(x, fit1[0] * x + fit1[1], color='blue')
plt.plot(x, fit2[0] * x + fit2[1], color='red')
plt.plot(x, fit3[0] * x + fit3[1], color='green')
plt.plot(x, fit4[0] * x + fit4[1], color='black')


# 添加标题和坐标轴标签
plt.xlabel('AST nodes')
plt.ylabel('Time(us)')
plt.legend(["Transform", "Analysis", "Translation", "Total"])


data=[fit1,fit2,fit3,fit4]
print("\\\\\n".join([" & ".join([str(float(x)) for x in line]) for line in data]) + "\\\\")

# 显示图像
plt.savefig('time_curve.png', dpi=400)