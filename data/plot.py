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

Y_SCALE = 5.0
Y_MIN = -90
Y_MAX = -30

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
processData = processNop

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

	def trim(self, t):
		i = 0
		while i < len(self.T) and self.T[i] < t - 2 * TIME_WIDTH:
			i += 1

		self.T = self.T[i:]
		self.Y = self.Y[i:]

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

colorMap = {}
dataMap = {}

def reportFrequencies():
	global dataMap

	prefix = 'Frequency: '
	clauses = []
	earliest = None
	latest = None
	total = 0

	for tag, line in dataMap.items():
		clauses.append("%s = %.3lf Hz" % (tag, 1.0 * len(line.T) / (line.T[-1] - line.T[0])))
		total += len(line.T)
		if latest is None or line.T[-1] > latest:
			latest = line.T[-1]
		if earliest is None or line.T[0] < earliest:
			earliest = line.T[0]

	clauses.append("total = %.3lf Hz" % (total / (1.0 * (latest - earliest))))

	print("%s %s" % (prefix, ', '.join(clauses)))

def reportMeans():
	global dataMap

	prefix = 'Mean: '
	clauses = []

	for tag, line in dataMap.items():
		clauses.append("%s = %.3lf" % (tag, np.mean(line.Y[-10:])))

	print("%s %s" % (prefix, ', '.join(clauses)))

def updateData(self):
	global colorMap
	global dataMap
	global ax

	while select2.select([sys.stdin], [], [], 0) == ([sys.stdin], [], []):
		tokens = input().split()

		try:
			primtag = tokens[0]
			subtag = tokens[1]
			value = float(tokens[2])

			tag = primtag + '-' + subtag
		except:
			print('Encountered invalid data point: ' + str(tokens))

		if tag not in dataMap:
			if primtag in colorMap:
				color = colorMap[primtag]
			else:
				color = COLORS[len(colorMap) % len(COLORS)]
				colorMap[primtag] = color
			dataMap[tag] = DataLine(ax, color + '-', tag, processData)

			lines = list(dataMap.values())
			ax.legend(
				[line.plot for line in lines],
				[line.label for line in lines]
			)
		data = dataMap[tag]

		data.append(time.time() - start_time, value)

		reportFrequencies()
		# reportMeans();

	t = time.time() - start_time
	for data in dataMap.values():
		data.trim(t)
		data.update(t)

simulation = animation.FuncAnimation(fig, updateData, blit=False, interval=20, repeat=False)
plt.show()
