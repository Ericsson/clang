#!/usr/bin/env python

import os
import re
import sys
import multiprocessing
import Queue
import threading
import stat
import argparse
import datetime
import subprocess
import itertools
from collections import namedtuple


#-------------- parse command line arguments --------------#

parser = argparse.ArgumentParser()

parser.add_argument('--processes', dest='processes',
                    help='How many concurrent analyzer processes to launch.',
                    default=multiprocessing.cpu_count())
parser.add_argument('--enable-checker', dest='enable_checker',
                    help='Checkers to enable.')
parser.add_argument('--disable-checker', dest='disable_checker',
                    help='Checkers to disable.')
parser.add_argument('--extra-enable-checker', dest='extra_enable_checker',
                    help='More checkers to enable.')
parser.add_argument('--extra-disable-checker', dest='extra_disable_checker',
                    help='More checkers to disable.')
parser.add_argument('--analyzer-config', dest='analyzer_config',
                    help='Extra options for the analyzer.')
parser.add_argument('--output-dir', dest='output_dir',
                    help='Extra options for the analyzer.')
parser.add_argument('--xtu-dir', dest='xtu_dir',
                    help='Absolute path to the .xtu directory.')
parser.add_argument('--no-remap', dest='no_remap',
                    help='Use an existing mapping of external functions.',
                    action="store_true")
parser.add_argument('--timeout', dest='timeout',
                    help='Timeout, in minutes, for stale clang processes.')

args = parser.parse_args()

xtu_dir = os.path.abspath(args.xtu_dir)

analyzer_params = []
if args.enable_checker:
    analyzer_params += [ '-analyzer-checker', args.enable_checker ]
if args.disable_checker:
    analyzer_params += [ '-analyzer-disable-checker', args.disable_checker ]
if args.extra_enable_checker:
    analyzer_params += [ '-analyzer-checker', args.extra_enable_checker ]
if args.extra_disable_checker:
    analyzer_params += [ '-analyzer-disable-checker', args.extra_disable_checker ]
if args.analyzer_config:
    analyzer_params += [ '-analyzer-config', args.analyzer_config ]
analyzer_params += [ '-analyzer-config', 'xtu-dir='+xtu_dir ]
analyzer_params += [ '-analyzer-stats' ]

out_dir = os.path.join(args.output_dir, 'xtu-analyze--' +
                       datetime.datetime.now()
                               .strftime('%Y-%m-%d--%H-%M-%S-%f'))
try:
    os.makedirs(out_dir)
except OSError:
    print 'Output directory %s already exists!' % out_dir
    sys.exit(1)

clang_path = os.environ.get('CLANG_PATH', "")
if clang_path :
    clang_path += "/"
else:
    from distutils.spawn import find_executable
    clang_path = find_executable("clang-cmdline-arch-extractor")
    clang_path = clang_path[0:len(clang_path)-len("clang-cmdline-arch-extractor")]
if not clang_path:
    print("Error: no sufficient clang found.")

clang_path = os.path.join(clang_path, 'clang')
ccc_analyzer_path = os.path.join(os.path.dirname(os.path.realpath(__file__)),
                                                              'ccc-analyzer')
analyzer_env = {}
analyzer_env['CLANG'] = clang_path
analyzer_env['OUT_DIR'] = xtu_dir
analyzer_env['CCC_ANALYZER_HTML'] = out_dir
analyzer_env['CCC_ANALYZER_ANALYSIS'] = ' '.join(analyzer_params)
analyzer_env['CCC_ANALYZER_OUTPUT_FORMAT'] = 'plist-multi-file'
if args.timeout is not None:
    analyzer_env['CCC_ANALYZER_TIMEOUT'] = str(60 * int(args.timeout))

main_funcs = set()
#-------------- obtain function-to-file mapping --------------#

print('Obtaining function-to-file mapping')
sys.stdout.flush()

if args.no_remap :
    print('An existing map will be used')
    sys.stdout.flush()
else :
    tmpdir = ".xtu/"

    fns = dict()
    external_map = dict()

    defined_fns_filename = tmpdir + "definedFns.txt"
    os.chmod(defined_fns_filename, stat.S_IRUSR)
    with open(defined_fns_filename,  "r") as defined_fns_file:
        for line in defined_fns_file:
            funcname, filename = line.strip().split(' ')
            if funcname.startswith('!') :
                funcname = funcname[1:]
                main_funcs.add(funcname)
            fns[funcname] = filename

    extern_fns_filename = tmpdir + "externalFns.txt"
    os.chmod(extern_fns_filename, stat.S_IRUSR)
    with open(extern_fns_filename,  "r") as extern_fns_file:
        for line in extern_fns_file:
            line = line.strip()
            if line in fns and not line in external_map :
                external_map[line] = fns[line]

    with open(tmpdir + "externalFnMap.txt",  "w") as out_file:
        for func, fname in external_map.items() :
            out_file.write("%s %s.ast\n" % (func, fname))

#-------------- analyze call graph to find analysis order --------------#

cfg = dict()
func_set = set()

print('Obtaining analysis order')
sys.stdout.flush()

callees_glob = set()
ast_regexp = re.compile("^/ast/(?:\w)+")

# Read call graph
cfg_filename = tmpdir + "cfg.txt"
os.chmod(cfg_filename, stat.S_IRUSR)
with open(cfg_filename,  "r") as cfg_file:
   for line in cfg_file:
       funcs = line.strip().split(' ')
       key = funcs[0]
       func_set.add(key)
       filename, func = key.split("::")
       callees = set()
       for callee in funcs[1:] :
           if callee.startswith("::") :
               fname = filename + callee
               callees.add(fname)
               func_set.add(fname)
           elif callee in external_map :
               arch = callee.split("@")[-1]
               fname = re.sub(ast_regexp, "", external_map[callee]) + \
                            "@" + arch + "::" + callee
               callees.add(fname)
               func_set.add(fname)
       if callees :
           cfg[key] = callees
           callees_glob |= callees


# Sort call graph in topological order

level = 0
to_out = set([ func for func in func_set \
    if func in cfg and not func in callees_glob ])
print "Level %d: %d" % (level, len(to_out))

top_level_list = []
file_order_set = set()

proceed_funcs = set()
proceed_files = set()
file_order = []

top_seed = [ func for func in func_set \
            if func in cfg and not func in callees_glob and func.split('::')[1] in main_funcs ]

while top_seed :
    func = top_seed.pop(0)
    if func in cfg :
        callees = cfg[func]
        for callee in callees :
            filename, funcname = callee.split('::')
            if not callee in proceed_funcs :
                top_seed.append(callee)
                proceed_funcs.add(callee)
                if not filename in proceed_files :
                    file_order.append(filename)
                    proceed_files.add(filename)
        del cfg[func]

remaining_funcs = { func for func in func_set if func not in proceed_funcs }

for func in remaining_funcs :
    filename, funcname = func.split('::')
    if not callee in proceed_funcs :
        proceed_funcs.add(callee)
        if not filename in proceed_files :
            file_order.append(filename)
            proceed_files.add(filename)

with open(tmpdir + "order.txt",  "w") as order_file :
    for filename in file_order :
        order_file.write(filename)
        order_file.write("\n")

# Ok, just sort it

BuildArgs = namedtuple("BuildArgs", "src_dir compiler cmd env")

build_args_dict = dict()

# Read build-args.txt
with open(tmpdir + "build-cmds.txt",  "r") as build_args_file :
    while True :
        file_list = build_args_file.readline().strip()
        if not file_list :
            break
        src_dir = build_args_file.readline().strip()
        compiler = build_args_file.readline().strip()
        gcc_cmd = build_args_file.readline().strip()
        env_var = build_args_file.readline().strip()

        for filename in file_list.split(" ") :
            build_args_dict[filename] = BuildArgs(src_dir, compiler, gcc_cmd, env_var)

print(len(build_args_dict))

with open(tmpdir + "dict.txt",  "w") as dict_file :
    dict_file.write(str(build_args_dict))

with open(tmpdir + "sorted.txt",  "w") as sorted_file:
    for filename in file_order :
        build_args = build_args_dict[filename]
        sorted_file.write(filename)
        sorted_file.write("\n")
        sorted_file.write(build_args.src_dir)
        sorted_file.write("\n")
        sorted_file.write(build_args.compiler)
        sorted_file.write("\n")
        sorted_file.write(build_args.cmd)
        sorted_file.write("\n")
        sorted_file.write(build_args.env)
        sorted_file.write("\n")
        del build_args_dict[filename]

    # Write remaining
    for key, value in build_args_dict.iteritems() :
        sorted_file.write(key)
        sorted_file.write("\n")
        sorted_file.write(value.src_dir)
        sorted_file.write("\n")
        sorted_file.write(value.compiler)
        sorted_file.write("\n")
        sorted_file.write(value.cmd)
        sorted_file.write("\n")
        sorted_file.write(value.env)
        sorted_file.write("\n")


# exit(0)

#-------------- finally run ccc-analyzer --------------#

args_queue = Queue.Queue()

def start_analyzer():
    while True:
        (src_dir, compiler, gcc_cmd) = args_queue.get()
        os.environ.update(analyzer_env)
        os.environ["COMPILER"] = compiler
        os.environ["IS_INTERCEPTED"] = "true"
        os.chdir(src_dir)
        cmd = ccc_analyzer_path + ' ' + gcc_cmd
        subprocess.call(cmd, shell=True)
        args_queue.task_done()

print '--= POOL OF %d PROCESSES RUNNING =--' % int(args.processes)
print 'Output path: %s' % out_dir
print 'XTU info path: %s' % xtu_dir
sys.stdout.flush()

for i in xrange(0, int(args.processes)):
    t = threading.Thread(target=start_analyzer)
    t.daemon = True
    t.start()

build_cmds = set()
build_cmds_n = 0
with open(".xtu/sorted.txt", "r") as build_args_file:
    while True:
        file_arch = build_args_file.readline().strip()
        src_dir = build_args_file.readline().strip()
        compiler = build_args_file.readline().strip()
        gcc_cmd = build_args_file.readline().strip()
        env_var = build_args_file.readline().strip()
        if not file_arch or not src_dir or not compiler or not gcc_cmd or not env_var :
            break
        build_cmds.add((src_dir, compiler, gcc_cmd))
        build_cmds_n += 1

print 'Total build commands: %d' % build_cmds_n
print 'Unique build commands: %d' % len(build_cmds)
sys.stdout.flush()

for (src_dir, compiler, gcc_cmd) in build_cmds:
    # FIXME: fix this earlier
    cmd = gcc_cmd.replace("\\\"", "\"")
    args_queue.put((src_dir, compiler, cmd))

# Put queue.join() into a daemon thread, makes interrupt work
term = threading.Thread(target = args_queue.join)
term.daemon = True
term.start()
try:
    while term.isAlive():
        term.join(999999999999) # just make sure it's a waited join
except KeyboardInterrupt:
    pass

print '--= POOL DONE =--'
sys.stdout.flush()

os.system('rm -vf .xtu/visitedFunc.txt')

try:
    os.removedirs(out_dir)
    print 'Removing directory %s because it contains no reports' % out_dir
except OSError:
    pass
