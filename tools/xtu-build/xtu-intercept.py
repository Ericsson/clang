#!/usr/bin/env python

from __future__ import print_function
from sys import argv
import os
from os import system
import re
import string
import subprocess
import random

clang_path = os.environ.get('CLANG_PATH', "")
if clang_path :
    clang_path += "/"
else:
    from distutils.spawn import find_executable
    clang_path = find_executable("clang-cmdline-arch-extractor")
    clang_path = clang_path[0:len(clang_path)-len("clang-cmdline-arch-extractor")]
if not clang_path:
    print("Error: no sufficient clang found.")

src_pattern = re.compile(".*\.(cc|c|cxx|cpp)$")
args = [clang_path + "clang", "-emit-ast"]
files = []
call_args = []

xtu_dir = os.environ.get('OUT_DIR', "")

clang_arch_bin = clang_path + "clang-cmdline-arch-extractor"


def get_ast_path(f, call_args):
    fullCmd = ' '.join([clang_arch_bin] + call_args + [f])
    output = subprocess.check_output(fullCmd, shell = True)
    arch = output[output.rfind('@')+1:].strip()
    ret = os.path.join(xtu_dir,
                        os.path.join("/ast/" + arch,
                                     os.path.realpath(f)[1:] + ".ast")[1:])
    try:
        os.makedirs(os.path.dirname(ret))
    except OSError:
        if os.path.isdir(os.path.dirname(ret)):
            pass # Dir already exists. Checking upfront is useless due to races.
        else:
            raise
    return ret

# FIXME: More precise filter is needed here.
for ind in range(1, len(argv)) :
    arg = argv[ind]
    if src_pattern.match(arg) :
        files.append(arg)
    else :
        call_args.append(arg)
call_args.append("-w")

print(' '.join(args + call_args ))
rets = [system(' '.join(args + call_args + [f, "-o", get_ast_path(f, call_args) ])) for f in files]
ret = 0
if len(filter(lambda x: x != 0, rets)) != 0 :
    ret = 1

if len(files) > 0:
    if system(' '.join([clang_path + "clang-func-mapping"] + ["--xtu-dir " + xtu_dir] + files + ["--"] + call_args)) != 0 :
        ret = 1

exit(ret)
