import matplotlib.pyplot as plt

# Function to extract the last column from a file
def extract_last_column(filename):
    with open(filename, 'r') as file:
        lines = file.readlines()
        last_values = [float(line.split()[-1]) for line in lines]
    return last_values

# Read data from files
static_values = extract_last_column('static.dat')
lib_values = extract_last_column('lib.dat')
tu_values = extract_last_column('tu.dat')

# Calculate min and max values for each dataset
static_min = min(static_values)
static_max = max(static_values)
static_avg = sum(static_values) / len(static_values)

lib_min = min(lib_values)
lib_max = max(lib_values)
lib_avg = sum(lib_values) / len(lib_values)

tu_min = min(tu_values)
tu_max = max(tu_values)
tu_avg = sum(tu_values) / len(tu_values)

# Data for plotting
labels = ['Static', 'TU', 'Lib',]
values = [static_avg, tu_avg, lib_avg]
errors = [(static_max - static_min), (tu_max - tu_min), (lib_max - lib_min)]

# Create the bar chart with error bars
fig, ax = plt.subplots()
hatch_patterns = ['/', '\\', 'x']
colors = ['0.4', '0.5', '0.7']

bars = ax.bar(labels, values, yerr=errors, capsize=5, color=colors)

ax.set_xlabel('Wrapper\'s Scope')
ax.set_ylabel('Relative overhead compared to PMPI')
ax.set_title('MPI_Comm_rank Instrumentation Overhead')

# Annotate with average, minimum, and maximum values next to error bars
for bar, value, error, avg, tmin, tmax in zip(bars, values, errors, [static_avg, tu_avg, lib_avg], [static_min, tu_min, lib_min], [static_max, tu_max, lib_max]):
    x = bar.get_x() + bar.get_width() / 2 + bar.get_width() / 10
    y = bar.get_height() + error + 0.001
    #ax.text(x, y, f'Max: {tmax:.3f}', ha='left', backgroundcolor="white")
    
    y = bar.get_height() - error - 0.002
    #ax.text(x, y, f'Min: {tmin:.3f}', ha='left', backgroundcolor="white")
    
    y = value
    ax.text(x, y, f'Avg: {avg:.3f}', ha='left', backgroundcolor="white")

ax.axhline(y=1.0,color="black",linestyle="dashed")

plt.tight_layout()
plt.savefig("interpos.eps")
plt.show()