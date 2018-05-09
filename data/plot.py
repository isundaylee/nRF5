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
COLORS = ['b', 'g', 'r', 'c', 'm', 'y', 'k', 'w']

Y_MIN = -10000
Y_MAX = 10000

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

def processSubtractMean(T, Y):
	pY = Y - mean(Y);
	pT = T[len(T)-len(pY):]

	return pT, pY

def processNop(T, Y):
	return T, Y

# processData = processRollingMedian
processData = processSubtractMean

################################################################################
# Animation plotting
################################################################################

class DataLine:
	def __init__(self, ax, style, label, processFn):
		self.T = np.zeros(0)
		self.Y = np.zeros(0)
		self.label = label
		self.processFn = processFn
		self.ax = ax
		self.plot, = ax.plot([], [], style, label=label)

	def append(self, t, y):
		self.T = np.append(self.T, t)
		self.Y = np.append(self.Y, y)

	def trim(self, count):
		if len(self.T) > count:
			self.T = self.T[len(self.T) - count:]
			self.Y = self.Y[len(self.Y) - count:]

	def update(self, t):
		self.plot.set_data(*self.processFn(self.T, self.Y))
		self.plot.axes.set_xlim(t - TIME_WIDTH, t)

fig = plt.figure(num = 0, figsize = (12, 8))
ax = plt.subplot()

start_time = time.time()

ax.set_ylim(Y_MIN, Y_MAX)
ax.set_xlim(0, TIME_WIDTH)
ax.grid(True)
ax.set_xlabel("Time (seconds)")
ax.set_ylabel("Value")

dataMap = {}

def updateData(self):
	global dataMap
	global ax

	while select2.select([sys.stdin], [], [], 0) == ([sys.stdin], [], []):
		tokens = input().split()
		tag = tokens[0]
		value = float(tokens[1])

		if tag not in dataMap:
			color = COLORS[len(dataMap) % len(COLORS)]
			dataMap[tag] = DataLine(ax, color + '-', tag, processData)

			lines = list(dataMap.values())
			ax.legend(
				[line.plot for line in lines],
				[line.label for line in lines]
			)
		data = dataMap[tag]

		data.append(time.time() - start_time, value)

	t = time.time() - start_time
	for data in dataMap.values():
		data.trim(DATA_KEEP)
		data.update(t)

simulation = animation.FuncAnimation(fig, updateData, blit=False, interval=20, repeat=False)
plt.show()
