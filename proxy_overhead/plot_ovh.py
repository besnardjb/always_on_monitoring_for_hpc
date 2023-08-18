import matplotlib.pyplot as plt
import numpy as np

# Frequencies (as strings)
frequencies = ['0.00001', '0.0001', '0.001', '0.01', '0.1', '1']

# Instrumentation cost (constant value)
instrumentation_cost = 4.1693048999999996e-09

# Initialize data storage
data = {freq: [] for freq in frequencies}

# Parse data from files
for freq in frequencies:
    filename = f"proxy-{freq}.dat"
    with open(filename, 'r') as file:
        lines = file.readlines()
        values = [float(line.split()[1]) for line in lines]
        data[freq] = values

# Calculate errors per series
errors = {freq: [np.mean(values) - np.min(values), np.max(values) - np.mean(values)] for freq, values in data.items()}

# Extract min and max values for error bars
min_errors = np.array([error[0] for error in errors.values()])
max_errors = np.array([error[1] for error in errors.values()])

# Calculate average values for bar heights (average time - instrumentation cost)
avg_values_adjusted = np.array([np.mean(values) - instrumentation_cost for values in data.values()])

# Create bar chart with error bars and stacked bars for instrumentation cost
fig, ax = plt.subplots()
bar = ax.bar(range(len(frequencies)), [instrumentation_cost] * len(frequencies), hatch='/', color='white', edgecolor='black', label='Library wrapper alone')
instrumentation_bar = ax.bar(range(len(frequencies)), avg_values_adjusted, yerr=[min_errors, max_errors], capsize=5, align='center', bottom=instrumentation_cost, edgecolor='black', label='Push Gateway counters')

ax.set_xticks(range(len(frequencies)))
ax.set_xticklabels(frequencies)
ax.set_xlabel('Measurement period')
ax.set_ylabel('Time in Seconds')
ax.set_title('Instrumentation overhead for MPI_Comm_rank')

# Add values at the center of each stacked plot
for i, (freq, value) in enumerate(zip(frequencies, avg_values_adjusted)):
    if freq == '0':
        continue  # Skip plotting value for index '0'
    ax.text(i, value / 2 + instrumentation_cost, f'{value:.2e}', ha='center', va='center', color='black', fontsize=10)

ax.legend()
plt.tight_layout()

# Show the plot
plt.savefig("ovh.eps")
plt.show()
