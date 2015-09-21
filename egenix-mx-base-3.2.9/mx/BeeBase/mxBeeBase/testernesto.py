#!/usr/local/bin/python

from mx.BeeBase import BeeStorage, BeeDict

BeeStorage._debug = 1
BeeDict._debug = 1

AUTOCOMMIT = 0

def fill():

    # Fill initial version of the dictionary
    d = BeeDict.BeeDict('testernesto', autocommit=AUTOCOMMIT)
    for i in range(10000):
        d[i] = "A"*i
    d.commit()
    #d.free_cache()
    d.close()


def update():

    # Update a few values
    d = BeeDict.BeeDict('testernesto', autocommit=AUTOCOMMIT)
    for i in range(100, 3000):
        d[i] = "C" * i
    d.commit()

import sys
mode = sys.argv[1]
if mode == 'init':
    import os
    for filename in ('testernesto.dat',
                     'testernesto.idx',
                     ):
      try:
        os.remove(filename)
      except:
        pass
    fill()

elif mode == 'update':
    update()
                
