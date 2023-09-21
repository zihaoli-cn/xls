import pandas as pd
import matplotlib.pyplot as plt

data = pd.read_csv("/root/xls/xls/p5/data/scheduling/result.csv")

for i in range(8):
    sample_data = data[data["ID"]==i]
    
    clk = None
    lines = []
    legend = ["ASAP", "SDC", "MinCut"]
    for s in legend:
        part = sample_data[sample_data["Strategy"]==s]
        
        clk = part["CLK"].to_numpy()
        lines.append(part["Quality"].to_numpy())

    print(clk)
    print(lines)
    
    plt.figure()
    plt.plot(clk, lines[0], label=legend[0])
    plt.plot(clk, lines[1], label=legend[1])
    plt.plot(clk, lines[2], label=legend[2])
    plt.xlabel("clk")
    plt.ylabel("Quality")
    plt.legend()
    plt.savefig("algorithms_quality_plot%d.png" % i)
    