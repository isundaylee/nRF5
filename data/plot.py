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
	
processData = processRollingMedian

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

plRaw, = ax.plot([], [], 'b-', label="Raw")
plProcessed, = ax.plot([], [], 'r-', label="Processed")

T = np.zeros(0)
Y = np.zeros(0)
def updateData(self):
	global T
	global Y
	
	while select2.select([sys.stdin], [], [], 0) == ([sys.stdin], [], []):
		Y = np.append(Y, float(input()))
		T = np.append(T, time.time() - start_time)
		
	if len(Y) > DATA_KEEP:
		Y = Y[len(Y)-DATA_KEEP:]
		T = T[len(T)-DATA_KEEP:]

	t = time.time() - start_time
	
	plProcessed.set_data(*processData(T, Y))
	plProcessed.axes.set_xlim(t - TIME_WIDTH, t)
	
	plRaw.set_data(T, Y)
	plRaw.axes.set_xlim(t - TIME_WIDTH, t)

simulation = animation.FuncAnimation(fig, updateData, blit=False, interval=20, repeat=False)
plt.show()
