import matplotlib.pyplot as plt

# Function to extract the second and third columns from a file
def extract_columns(filename):
    with open(filename, 'r') as file:
        lines = file.readlines()
        with_values = [float(line.split()[1]) for line in lines]
        without_values = [float(line.split()[2]) for line in lines]
    return with_values, without_values

# Read data from files
static_with_values, static_without_values = extract_columns('static.dat')
lib_with_values, lib_without_values = extract_columns('lib.dat')
tu_with_values, tu_without_values = extract_columns('tu.dat')

# Calculate averages for each dataset
static_with_avg = sum(static_with_values) / len(static_with_values)
static_without_avg = sum(static_without_values) / len(static_without_values)

lib_with_avg = sum(lib_with_values) / len(lib_with_values)
lib_without_avg = sum(lib_without_values) / len(lib_without_values)

tu_with_avg = sum(tu_with_values) / len(tu_with_values)
tu_without_avg = sum(tu_without_values) / len(tu_without_values)

# Data for plotting
labels = ['Static', 'TU', 'Lib']
with_avgs = [static_with_avg, tu_with_avg, lib_with_avg]
without_avgs = [static_without_avg, tu_without_avg, lib_without_avg]


print(with_avgs)
print(without_avgs)

for i in range(0, len(with_avgs)):
    with_avgs[i] = with_avgs[i] - without_avgs[i]

# Define hatch patterns for each dataset
hatch_patterns = ['/', '-', 'x']

# Create the bar chart with hatch patterns and grayscale colors
fig, ax = plt.subplots()
without_bars = ax.bar(labels, without_avgs, hatch=hatch_patterns[1], color='0.8', alpha=1, label='Uninstrumented')
with_bars = ax.bar(labels, with_avgs, hatch=hatch_patterns[0], bottom=without_avgs, color='#F00', label='Instrumented')


ax.set_xlabel('Wrapper\'s Scope')
ax.set_ylabel('Time in Seconds')
ax.set_title('MPI_Comm_rank Instrumentation Overhead')
ax.legend()

plt.tight_layout()
plt.savefig("interpos_time.eps")
plt.show()
