import numpy as np
import pandas as pd
import select as select2
import time
import matplotlib.animation as animation

from matplotlib.pylab import *
from mpl_toolkits.axes_grid1 import host_subplot

################################################################################
# Constants
################################################################################

TIME_WIDTH = 30
DATA_KEEP = 20 * TIME_WIDTH

Y_MIN = -90.0
Y_MAX = -30.0

################################################################################
# Different data post-processing methods
################################################################################
def processRollingMax(T, Y):
	pY = pd.Series(Y).rolling(10).max().dropna().rolling(10).mean().tolist()
	pT = T[len(T)-len(pY):]

	return pT, pY

def processRollingMedian(T, Y):
	pY = pd.Series(Y).rolling(10).median().dropna().tolist();
	pT = T[len(T)-len(pY):]

	return pT, pY

def processNop(T, Y):
	return T, Y

processData = processRollingMedian
# processData = processNop

################################################################################
# Animation plotting
################################################################################
fig = plt.figure(num = 0, figsize = (12, 8))
ax = plt.subplot()

start_time = time.time()

ax.set_ylim(Y_MIN, Y_MAX)
ax.set_xlim(0, TIME_WIDTH)
ax.grid(True)
ax.set_xlabel("Time (seconds)")
ax.set_ylabel("RSSI")

plRaw1, = ax.plot([], [], 'b-', label="Raw")
plRaw2, = ax.plot([], [], 'r-', label="Raw")
plRaw3, = ax.plot([], [], 'g-', label="Raw")
# plProcessed, = ax.plot([], [], 'r-', label="Processed")

T1 = np.zeros(0)
T2 = np.zeros(0)
T3 = np.zeros(0)
Y1 = np.zeros(0)
Y2 = np.zeros(0)
Y3 = np.zeros(0)
def updateData(self):
	global T1, T2, T3
	global Y1, Y2, Y3

	while select2.select([sys.stdin], [], [], 0) == ([sys.stdin], [], []):
		tokens = input().split()
		ch = int(tokens[0])
		value = float(tokens[1])

		if ch == 37:
			Y1 = np.append(Y1, value)
			T1 = np.append(T1, time.time() - start_time)
		elif ch == 38:
			Y2 = np.append(Y2, value)
			T2 = np.append(T2, time.time() - start_time)
		elif ch == 39:
			Y3 = np.append(Y3, value)
			T3 = np.append(T3, time.time() - start_time)

	if len(Y1) > DATA_KEEP:
		Y1 = Y1[len(Y1)-DATA_KEEP:]
		T1 = T1[len(T1)-DATA_KEEP:]
	if len(Y2) > DATA_KEEP:
		Y2 = Y2[len(Y2)-DATA_KEEP:]
		T2 = T2[len(T2)-DATA_KEEP:]
	if len(Y3) > DATA_KEEP:
		Y3 = Y3[len(Y3)-DATA_KEEP:]
		T3 = T3[len(T3)-DATA_KEEP:]

	t = time.time() - start_time

	# plProcessed.set_data(*processData(T, Y))
	# plProcessed.axes.set_xlim(t - TIME_WIDTH, t)

	T1P, Y1P = processData(T1, Y1)
	T2P, Y2P = processData(T2, Y2)
	T3P, Y3P = processData(T3, Y3)

	print("Ch 37 = %f; Ch 38 = %f; Ch 39 = %f;" %
		(np.mean(Y1P), np.mean(Y2P), np.mean(Y3P)))

	plRaw1.set_data(T1P, Y1P)
	plRaw1.axes.set_xlim(t - TIME_WIDTH, t)
	plRaw2.set_data(T2P, Y2P)
	plRaw2.axes.set_xlim(t - TIME_WIDTH, t)
	plRaw3.set_data(T3P, Y3P)
	plRaw3.axes.set_xlim(t - TIME_WIDTH, t)

simulation = animation.FuncAnimation(fig, updateData, blit=False, interval=20, repeat=False)
plt.show()
