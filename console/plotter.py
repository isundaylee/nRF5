import sys
import os
import datetime

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

PLOT_LIMIT = 48 * 3600

if len(sys.argv) < 3:
    print("Usage: python3 plotter.py <output_dir_path> <node_addr>")
    exit(1)

output_path = sys.argv[1]
node_addr = int(sys.argv[2])

battery_ts = []
battery_values = []

friendless_ts = []
friendless_values = []

with open(os.path.join(output_path, "protocol_transcript")) as f:
    for line in f:
        tokens = line.strip().split(" ")

        if len(tokens) < 2:
            continue

        if tokens[1] != "sta" or (tokens[2] not in ("health", "battery")):
            continue

        if int(tokens[3]) != node_addr:
            continue

        ts = datetime.datetime.fromtimestamp(float(tokens[0]))
        op = tokens[2]

        if op == "battery":
            battery_ts.append(ts)
            battery_values.append(float(tokens[6]) * 6.0 * 0.6 / float(1 << 14))
        elif op == "health":
            friendless_ts.append(ts)
            friendless_values.append(1 if "01" in tokens[6] else 0)

# Create DataFrames
df_battery = pd.DataFrame({"timestamp": battery_ts, "voltage": battery_values})
df_friendless = pd.DataFrame(
    {"timestamp": friendless_ts, "friendless": friendless_values}
)

# Plot only the last PLOT_LIMIT seconds
datetime_cutoff = df_battery.iloc[-1]["timestamp"] - datetime.timedelta(
    seconds=PLOT_LIMIT
)
df_battery = df_battery[df_battery["timestamp"] > datetime_cutoff]
df_friendless = df_friendless[df_friendless["timestamp"] > datetime_cutoff]

# Finally, let's plot it :)
plt.plot(df_battery["timestamp"], df_battery["voltage"].rolling(10).mean())
plt.plot(df_friendless["timestamp"], df_friendless["friendless"] + 2)
plt.show()
