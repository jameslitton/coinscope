#!/usr/bin/python

import numpy as np
from collections import defaultdict
import matplotlib as mpl
import matplotlib.pyplot as pyplot
import matplotlib.patches as mpatches
import copy as copy
import sys
import random;

random.seed(20)



def get_keys(targets,data_f):
    global ct00


    ct00,my_mints = concern_graph(data_f, targets)
    pretty(ct00,my_mints)

def pick(file, K):
    possible = set();
    with open(file, 'r') as fp:
	for i,line in enumerate(fp):
	    src,tgt,report,ts = line.strip().split(',')
	    possible.add(tgt)
    return random.sample(possible, K)

def concern_graph(fn, good):
    import subprocess

    # Select some random nodes we're interested in
    concern = list(good)[:(140)]
    ct = dict((c,defaultdict(lambda:0)) for c in concern)
    mints = 1e50

    f = open(fn)
    f.next()
    f.next()
    for i,line in enumerate(f):
        src,tgt,report,ts = line.strip().split(',')
        report = int(report)
        ts = float(ts)
        mints = min(mints,ts)
        # Only care about reports about a node
        if tgt in concern:
            ct[tgt][report] += 1
        #if not i%300000: print i
    for k in ct: ct[k] = dict(ct[k])
    f.close()
    return ct, mints


def pretty(ct,mints):
    xs = []
    ys = []
    cs = []
    xraw = []
    ips = []
    dt = {}

    for k in ct:
        dt[k] = dict((ip,count) for ip,count in ct[k].iteritems() if count > 1)
    #ct = dt

    times = []
    for i,ip in list(enumerate(ct))[:(140)]:
        try:
            tmax = max(t for t in ct[ip] if ct[ip][t] > 9)
        except ValueError:
            tmax = max(t for t in ct[ip])


	it = []
	for report in ct[ip]:
	    it.append(report-tmax)

	times.append((i,max(it)))

    times.sort(key=lambda x : x[1])

    exclude = set()
    for t in times[:10]:
	exclude.add(t[0])

    for t in times[110:]:
	exclude.add(t[0])

    xraw = []
    for idx,ip in list(enumerate(ct))[:140]:
        try:
            tmax = max(t for t in ct[ip] if ct[ip][t] > 9)
        except ValueError:
            tmax = max(t for t in ct[ip])

	if idx not in exclude:
	    xraw.append([])
	    for report in ct[ip]:
		ips.append(ip)
		ys.append(len(xraw)-1)
		xs.append((report-tmax)/3600.)
		cs.append(ct[ip][report])
		xraw[-1].append(report-tmax)
	    if not xraw[-1]: xraw.pop()

    pyplot.clf()
    inds = np.argsort(map(max, xraw))
    tbl = copy.copy(inds)
    for i,y in enumerate(inds): tbl[y] = i
    inds = tbl
    for i in range(len(inds)):
        #pyplot.plot([-100,24],[i,i],color=(0.8,0.8,0.8),zorder=2)
        ys[i] = inds[y]
    xs = np.array(xs)
    #xs[np.array(cs) <= 2] = inf
    #xs = np.array(xs)[inds]
    #ys = np.array(ys)[inds]
    #cs = np.array(cs)[inds]
    pyplot.scatter(xs,tbl[ys],c=np.log10(cs),s=4*np.log2(cs)+1,zorder=3,vmin=0,vmax=4)

    yline = -4
    pyplot.plot([-100,100], [yline,yline], 'k')
    pyplot.ylim(-1, 100);
    xs = np.array(xs)
    #xs[np.array(cs) <= 2] = inf
    #scatter(xs,yline-4-np.array(ys)*2,c=np.log10(cs),s=4*np.log2(cs)+1,zorder=3,vmin=0,vmax=5)

    pyplot.xticks(np.arange(-100,100,4))
    pyplot.grid(axis='x')

    mx = np.percentile(xs,99.9)
    pyplot.xlim([-24,16])
    #xlim(mx-16, mx+1)
    bar = (mints-20*60)/3600.
    pyplot.plot([bar,bar],[min(ys)-1,max(ys)+1])
    ax = pyplot.gca()
    import matplotlib.dates
    #ax.xaxis.set_major_locator(matplotlib.dates.AutoDateLocator())
    #pyplot.title('Frequency of addr timestamps about nodes')
    pyplot.ylabel('Nodes (100 randomly chosen out of ~6k)')
    pyplot.xlabel('Time (hours since first time >10 nodes share same timestamp for target node)')

    pyplot.text(14, 93, "1", fontsize=14, weight=500, color='white', 
		horizontalalignment='center', verticalalignment='center', family='sans-serif')
    pyplot.plot(14, 93, 'or', markersize=18, markeredgecolor='red');

    pyplot.text(14, 96, "2", fontsize=14, weight=500, color='white', 
		horizontalalignment='center', verticalalignment='center', family='sans-serif')
    pyplot.plot(14, 96, 'or', markersize=18, markeredgecolor='red');


    fig1 = pyplot.gcf()
    pyplot.colorbar()

    pyplot.show()
    pyplot.draw()

    #fig1.savefig('bubble_plots.eps')

    #pyplot.tight_layout();
    #pyplot.show()
    #pyplot.draw()
    
    #fig1.savefig('bubble_plots.eps')
    #pyplot.close()

if __name__ == '__main__':
    argv = sys.argv
    if (len(argv) <= 1):
	sys.exit(0);
    elif (len(argv) == 2):
	observations = argv[1]
	target_nodes = pick(observations, 140)
    else:
	observations = argv[1]
	target_nodes = [];
	with open(argv[2], "r") as fp:
	    for lines in fp:
		target_nodes.append(lines.strip())

    get_keys(target_nodes,observations)

# def fix_key_graph(kg):
#     inv = {}
#     for rep,ips in kg.iteritems():
#         for ip in ips:
#             if ip in inv: inv[ip] = max(rep,inv[ip])
#             else: inv[ip] = rep
#     kg2 = defaultdict(lambda:set())
#     for ip,rep in inv.iteritems():
#         kg2[rep].add(ip)
#     return dict(kg2)
        

# def birth_death_jump(key_graphs, key):
#     # Parse a sequence of files, for each one recording all the edges incident to this node
#     clf()
#     tmax = max(key_graphs[0])
#     for i in range(len(key_graphs)):
#         print 'Beginning Round', i
#         graph = fix_key_graph(key_graphs[i])
#         for k in graph:
#             graph[k] = tuple(set(graph[k]))
#         ts = key_edges[i][0]
#         # First just plot the graph
#         height = -ts
#         plot([-100,100],[height,height],color=(0.8,0.8,0.8),zorder=2)
#         cs = map(len, graph.values())
#         xs = (np.array(graph.keys())-tmax)/3600.
#         xs[cs==0] = inf
#         ys = len(graph)*[height]
#         scatter(xs,ys,c=np.log10(cs),s=4*np.log2(cs)+1,zorder=3,vmin=0,vmax=4)
#         print 'scatter once'
#         if i == 0: continue
#         print 'matching'


#         # Find the matches
#         gprev = fix_key_graph(key_graphs[i-1])
#         tsprev = key_edges[i-1][0]
#         births = defaultdict(lambda:0)
#         deaths = defaultdict(lambda:0)
#         # First prepare the back index, for the previous line
#         invp = {}
#         invn = {}
#         for report,ips in gprev.iteritems():
#             for ip in set(ips):
#                 invp[ip] = max(report,invp[ip] if ip in invp else 0)
#         for report,ips in graph.iteritems():
#             for ip in set(ips):
#                 if -height == 10 and 8.5 < (report-tmax)/3600. < 9.5:
#                     print ip
#                 invn[ip] = max(report,invn[ip] if ip in invn else 0)

#         # Store the thickness of each line
#         reportcount = defaultdict(lambda:0)

#         # Count the number of correspondences between reports in past frame
#         for ip,report in invn.iteritems():
#             if ip in invp:
#                 if invp[ip] > report:
#                     print 'backwards:', ip, invp[ip], report
#                 reportcount[(invp[ip],report)] += 1

#         # births
#         for ip in invn:
#             if ip not in invp:
#                 births[invn[ip]] += 1

#         # deaths
#         for ip in invp:
#             if ip not in invn:
#                 deaths[invp[ip]] += 1

#         scalar = np.array([0.9,0.9,0.9,0.7])
#         cm = matplotlib.cm.ScalarMappable()
#         cm.set_clim(vmin=0,vmax=4)
#         for (repa,repb),count in reportcount.iteritems():
#             if count == 0: continue
#             c = cm.to_rgba(np.log10(count))
#             c = np.array(c)
#             c *= scalar
#             plot([(repa-tmax)/3600.,(repb-tmax)/3600.],[-(tsprev),-ts],c=c,linewidth=0.4*np.log2(count)+1)

#         yyy = '24.30.18.18'
#         if yyy in invn:
#             print invn[yyy]
#             plot((invn[yyy]-tmax)/3600.,height,'k^',markersize=15,linewidth=5)

#         for birth,count in births.iteritems():
#             if count == 0: continue
#             c = cm.to_rgba(np.log10(count))
#             c = np.array(c)
#             c *= scalar
#             plot([(birth-tmax)/3600.-0.4,(birth-tmax)/3600.], [height+.5,height], c=c, linewidth=0.4*np.log2(count)+1)

#         for death,count in deaths.iteritems():
#             c = cm.to_rgba(np.log10(count))
#             c = np.array(c)
#             c *= scalar
#             plot([(death-tmax)/3600.-0.4,(death-tmax)/3600.], [-(tsprev)-.5,-tsprev], c=c, linewidth=0.4*np.log2(count)+1)

#         #plot([report-0.2,report],[height,height-0.2],'k')
#         # It's a Birth

#     xticks(np.arange(-100,100,2))
#     grid(axis='x')
#     xlim(-12,15)
#     ylabel('Probe time (hours)')
#     title('Birth, Death, and Migration of AddrMan entries, by node')
#     xlabel('Hours (0 points?)')


# def focus_graph():
#     # Draw the Key!
#     xs = []
#     ys = []
#     cs = []
#     xraw = []
#     ips = []
#     dt = {}
#     ctsi = [(0,ct00),
#             (0.25,ct15),
#             (0.50,ct30),
#             (1,ct60),
#             (2,ct120),
#             (4,ct240),(6,ct360),(8,ct480),(10,ct600),(12,ct720),(14,ct840)]
#     for i,(tt,ctX) in enumerate(ctsi):
#         ip = key_ip
#         maxx = max(ctX[ip])
#         #try: 
#         tmax = max(t for t in ctX[ip] if ctX[ip][t] > 9 and t <= maxx - 2*60*60)
#         #except ValueError: tmax = max(t for t in ctX[ip])
#         #tmax = max(t for t in ct00[ip])
#         #tmax = max(t for t in ct120[ip] if ct120[ip][t] > 120)
#         xraw.append([])
#         for report in ctX[ip]:
#             ips.append(ip)
#             #ys.append(len(xraw)-1)
#             ys.append(tt)
#             xs.append((report-tmax)/3600.)
#             cs.append(ctX[ip][report])
#             xraw[-1].append(report-tmax)
#             yline = -4
#         plot([-100,100],[yline-4-tt*2,yline-4-tt*2],color=(0.8,0.8,0.8),zorder=2)


