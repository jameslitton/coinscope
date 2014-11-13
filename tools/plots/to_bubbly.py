#!/usr/bin/python

import sys;
import os;
import cPickle;

def as_key(x):
   return ':'.join(map(str,x))


def find_center(p):

   # why is this so convoluted? Because I was too fucking stupid to do
   # it in the straightforward way, due to lack of sleep. I traced and
   # copied what's in bubble_plots.py in four different places, which
   # is a shitfest, and we have as below

   ct = dict();
   for tgt in p.keys():
      key = as_key(tgt)
      ct[as_key(tgt)] = dict()
      for report in p[tgt]:
	 for it in p[tgt][report]:
	    src = as_key(it[0])
	    ts = float(it[1])
	    if not ct[as_key(tgt)].has_key(report):
	       ct[as_key(tgt)][report] = 0
	    ct[as_key(tgt)][report] += 1

   d = dict()
   for ip in ct.keys():
      tmax = 0
      try:
	 tmax = max(t for t in ct[ip] if ct[ip][t] > 9)
      except ValueError:
	 tmax = max(t for t in ct[ip])
      d[ip] = tmax;


   return d
	    

def convert(p, outfp, center):
   for tgt in p.keys():
      key = as_key(tgt)
      for report in p[tgt]:
	 rightoline = (center.has_key(key) and int(report) >= center[key])
	 if len(p[tgt][report]) > 1 or rightoline:
	    for it in p[tgt][report]:
	       src = as_key(it[0])
	       ts = it[1]
	       if rightoline:
		  if (float(ts) - float(report)) < (60*60*2):
		     s = "%s,%s,%d,%f\n" % (src, as_key(tgt), report, ts)
		     outfp.write(s);
	       else:
		     s = "%s,%s,%d,%f\n" % (src, as_key(tgt), report, ts)
		     outfp.write(s);

		  
for file in sys.argv[1:]:
   print "Trying " + file
   idx = file.find(".pkl")
   if (idx >= 0):
      if os.path.exists(file) and not os.path.exists(file[:idx] + ".converted"):
	 with open(file, 'r') as infp:
	    p = cPickle.load(infp);
	    center = find_center(p)
	    with open(file + ".converted.tmp", 'w') as outfp:
	       convert(p, outfp, center)
	 
	 os.rename(file + ".converted.tmp", file[:idx] + ".converted")
