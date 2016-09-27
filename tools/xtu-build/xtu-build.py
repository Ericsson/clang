#!/usr/bin/env python

from sys import argv
import os
import shutil

xtu_dir = os.getenv("OUT_DIR", ".xtu")
script_dir = os.path.dirname(__file__)

if os.path.exists(xtu_dir) :
    shutil.rmtree(xtu_dir)
os.mkdir(xtu_dir)
os.system("%s/strace_interceptor.py %s" % (script_dir, ' '.join(argv[1:])))
# Just in case it's accidentally empty:
os.system('touch ' + os.path.join(xtu_dir, 'externalFns.txt'))
