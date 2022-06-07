import matplotlib.pyplot as plt
import json
from pandas import json_normalize

def addlabels(x, y_sdc, y_cut):
    for i in range(len(x)):
        boost = 100.0 * (y_cut[i] - y_sdc[i])/float(y_cut[i])
        plt.text(x[i], y_cut[i], "%.2f%%" % (boost))


def vis_register(name, n, clks, bits_sdc, bits_min_cut, w : float):
    plt.figure()

    plt.bar([x - 0.5 * w for x in range(n)], bits_sdc, label="SDC", width=w, color="r")
    plt.bar([x + 0.5 * w for x in range(n)], bits_min_cut, label="MinCut", width=w, color="b")

    addlabels([x - 1.5*w for x in range(n)], bits_sdc, bits_min_cut)

    plt.xlabel("clk(ps)")
    plt.ylabel("pipeline register(bit)")

    plt.title(name)
    plt.legend()
    plt.xticks([x for x in range(n)], clks)
    plt.grid(linestyle="-.", alpha=0.4)

    plt.savefig('%s_register.png' % name, dpi=400)
    plt.close()


def vis_runing_time(name, n, clks, t_sdc, t_sdc_solver, t_cut, w : float):
    plt.figure()
    
    plt.bar([x - 0.5 * w for x in range(n)], t_sdc, label="SDC-Other", width=w, color="r")
    plt.bar([x - 0.5 * w for x in range(n)], t_sdc_solver, label="SDC-Solver", width=w, color="g")
    plt.bar([x + 0.5 * w for x in range(n)], t_cut, label="MinCut", width=w, color="b")

    plt.xlabel("clk(ps)")
    plt.ylabel("time(s)")
    
    plt.title(name)
    plt.legend()
    plt.xticks([x for x in range(n)], clks)
    plt.grid(linestyle="-.", alpha=0.4)

    plt.savefig('%s_time.png' % name, dpi=400)
    plt.close()


def vis_runing_time2(name, n, clks, t_sdc, t_sdc_solver, t_sdc_fast, t_sdc_solver_fast, t_cut, w : float):
    plt.figure()
    
    plt.bar([x - w for x in range(n)], t_sdc, label="SDC-Other", width=w, color="r")
    plt.bar([x - w for x in range(n)], t_sdc_solver, label="SDC-Solver", width=w, color="g")
    plt.bar([x for x in range(n)], t_cut, label="MinCut", width=w, color="b")
    plt.bar([x + w for x in range(n)], t_sdc_fast, label="SDC2-Other", width=w, color="y")
    plt.bar([x + w for x in range(n)], t_sdc_solver_fast, label="SDC2-Solver", width=w, color="c")

    plt.xlabel("clk(ps)")
    plt.ylabel("time(s)")
    
    plt.title(name)
    plt.legend()
    plt.xticks([x for x in range(n)], clks)
    plt.grid(linestyle="-.", alpha=0.4)

    plt.savefig('%s_time2.png' % name, dpi=400)
    plt.close()


def vis_overall_table(overall):
    fig, ax = plt.subplots()
    # hide axes
    fig.patch.set_visible(False)
    ax.axis('off')
    ax.axis('tight')

    tbl = ax.table(cellText=overall.values, colLabels=overall.columns, loc='center')
    tbl.auto_set_font_size(False)
    tbl.set_fontsize(7)
    fig.tight_layout()
    fig.savefig("overview.png", dpi=400)


def vis_constraints_running_time(name, clk, n_clock_related_constraints, t_sdc_solver, n_edges):
    plt.figure()
    t = [x/t_sdc_solver[0] for x in t_sdc_solver]
    y = [float(x) / n_edges for x in n_clock_related_constraints]
    plt.plot(clk, t, "r-", label="time")
    plt.plot(clk, y, "g-", label="constraints")
    plt.legend()
    plt.title(name)
    plt.xlabel("clk(ps)")
    plt.ylabel("ratio")
    plt.grid(linestyle="-.", alpha=0.4)
    plt.savefig("%s_analysis.png" % name, dpi=400)
    plt.close()

def vis_constraints_running_time2(name, clk, n_clock_related_constraints, t_sdc_solver, n_clock_related_constraints2, t_sdc_solver2, n_edges):
    fig, axs = plt.subplots(2, sharex=True)
    fig.suptitle('%s: #(Constrints) vs Runnint Time' % name)
    axs[0].plot(clk, t_sdc_solver, "r-", label="SDC1")
    axs[0].plot(clk, t_sdc_solver2, "g-", label="SDC2")
    axs[1].plot(clk, n_clock_related_constraints, "r-", label="SDC1")
    axs[1].plot(clk, n_clock_related_constraints2, "g-", label="SDC2")
    
    axs[0].legend(loc="upper right")
    axs[1].legend(loc="upper right")
    
    axs[0].set_ylabel("time(s)")

    axs[1].set_ylabel("number of constraints")
    axs[1].set_xlabel("clk(ps)")

    fig.savefig("%s_analysis2.png" % name, dpi=400)


data = json.load(open("result.json", "r"))
for benchmark in data:
    benchmark["name"] = benchmark["name"].split("/")[-1]

for benchmark in data:
    n = len(benchmark["data"])
    clks = [x["clk"] for x in benchmark["data"]]
    bits_sdc = [x["bits_sdc"] for x in benchmark["data"]]
    bits_min_cut = [x["bits_min_cut"] for x in benchmark["data"]]
   
    t_sdc_constraints = [x["constraints_computing_time"] for x in benchmark["data"]]
    t_sdc_solver = [x["t_sdc_solver"] for x in benchmark["data"]]
    t_sdc_fast_constraints = [x["constraints_computing_time2"] for x in benchmark["data"]]
    t_sdc_fast_solver = [x["t_sdc_solver2"] for x in benchmark["data"]]

    t_cut = [x["t_min_cut"] for x in benchmark["data"]]
    t_sdc = [x["t_sdc"] for x in benchmark["data"]]
    t_sdc_fast = [x["t_sdc_fast"] for x in benchmark["data"]]

    n_constraints = [x["constraints"] for x in benchmark["data"]]
    n_clock_related_constraints = [x["clock_related_constraints"] for x in benchmark["data"]]
    
    n_constraints2 = [x["constraints2"] for x in benchmark["data"]]
    n_clock_related_constraints2 = [x["clock_related_constraints2"] for x in benchmark["data"]]
    
    name = benchmark["name"]
    vis_register(name, n, clks, bits_sdc, bits_min_cut, 0.3)
    
    vis_runing_time(name, n, clks, t_sdc, t_sdc_solver, t_cut, 0.3)
    vis_runing_time2(name, n, clks, t_sdc, t_sdc_solver, t_sdc_fast, t_sdc_fast_solver, t_cut, 0.25)

    if name in ["sha256", "idct_chen"]:
        vis_constraints_running_time(name, clks, n_clock_related_constraints, t_sdc_solver, benchmark["edges"])
        vis_constraints_running_time2(name, clks, n_clock_related_constraints, t_sdc_solver, n_clock_related_constraints2, t_sdc_fast_solver, benchmark["edges"])

each = json_normalize(data, "data")
overall = json_normalize([{k:v for (k, v) in benchmark.items() if k != "data"} for benchmark in data])
vis_overall_table(overall)