#!/usr/bin/env python

from __future__ import print_function
import os
import sys
import subprocess
import multiprocessing

def start_analyzer(cmd) :
    print(cmd)
#    os.environ.update(env)
    return os.system(cmd)

analyzer_process_pool = multiprocessing.Pool(processes = int(os.environ.get('NUM_PROCESSES', 
                                            multiprocessing.cpu_count())))

build_cmd_filename = ".xtu/build-cmds.txt"
if os.stat(build_cmd_filename)[6] == 0 :
    exit(0)
cmd_list = open(build_cmd_filename, "r")

script_dir = os.path.dirname(__file__)
out_dir = os.getcwd() + "/.xtu"

work_dir = cmd_list.readline()
args = cmd_list.readline()
env = cmd_list.readline()

while work_dir :
    print("cd %s && %s %s/ccc-analyzer %s" % \
                                      (work_dir.rstrip(), env.rstrip(), \
                                      script_dir, args.rstrip()))
    analyzer_process_pool.apply_async(start_analyzer, \
                                      ["cd %s && %s IS_INTERCEPTED=true OUT_DIR=%s %s/ccc-analyzer %s" % \
                                      (work_dir.rstrip(), env.rstrip(), out_dir, \
                                      script_dir, args.rstrip())])
    work_dir = cmd_list.readline()
    args = cmd_list.readline()
    env = cmd_list.readline()

cmd_list.close()

analyzer_process_pool.close()
analyzer_process_pool.join()
