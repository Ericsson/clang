#!/usr/bin/env python

import argparse
import io
import json
import multiprocessing
import os
import re
import shutil
import signal
import subprocess
import string
import sys
import threading
import time
import uuid

threading_factor = int(multiprocessing.cpu_count() * 1.0)
analyser_output_formats = ['plist-multi-file', 'plist', 'plist-html',
                           'html', 'text']
analyser_output_format = analyser_output_formats[0]

parser = argparse.ArgumentParser(
            description='Executes 2nd pass of XTU analysis')
parser.add_argument('-b', required=True, dest='buildlog', metavar='build.json',
                    help='Use a JSON Compilation Database')
parser.add_argument('-p', metavar='preanalyze-dir', dest='xtuindir',
                    help='Use directory for reading preanalyzation data '
                         '(default=".xtu")',
                    default='.xtu')
parser.add_argument('-o', metavar='output-dir', dest='xtuoutdir',
                    help='Use directory for output analyzation results '
                         '(default=".xtu-out")',
                    default='.xtu-out')
parser.add_argument('-e', metavar='enabled-checker', nargs='+',
                    dest='enabled_checkers',
                    help='List all enabled checkers')
parser.add_argument('-d', metavar='disabled-checker', nargs='+',
                    dest='disabled_checkers',
                    help='List all disabled checkers')
parser.add_argument('-j', metavar='threads', dest='threads',
                    help='Number of threads used (default=' +
                    str(threading_factor) + ')',
                    default=threading_factor)
parser.add_argument('-v', dest='verbose', action='store_true',
                    help='Verbose output of every command executed')
parser.add_argument('--clang-path', metavar='clang-path', dest='clang_path',
                    help='Set path of clang binaries to be used (default '
                         'taken from CLANG_PATH environment variable)',
                    default=os.environ.get('CLANG_PATH'))
parser.add_argument('--analyze-cc-path', metavar='analyze-cc-path',
                    dest='analyze_path',
                    help='Set path of analyze-cc to be used '
                         '(default is taken from CLANG_ANALYZE_CC_PATH '
                         'environment variable)',
                    default=os.environ.get('CLANG_ANALYZE_CC_PATH'))
parser.add_argument('--output-format', metavar='format',
                    choices=analyser_output_formats,
                    default=analyser_output_format,
                    help='Format for analysis reports '
                         '(one of %s; default is "%s").' %
                    (', '.join(analyser_output_formats),
                     analyser_output_format))
parser.add_argument('--no-xtu', dest='no_xtu', action='store_true',
                    help='Do not use XTU at all, '
                         'only do normal static analysis')
mainargs = parser.parse_args()

concurrent_threads = 0
concurrent_thread_times = [0.0]
concurrent_thread_last_clock = time.time()

if mainargs.clang_path is None:
    clang_path = ''
else:
    clang_path = os.path.abspath(mainargs.clang_path)
if mainargs.verbose:
    print 'XTU uses clang dir: ' + (clang_path if clang_path != ''
                                    else '<taken from PATH>')

if mainargs.analyze_path is None:
    analyze_path = ''
else:
    analyze_path = os.path.abspath(mainargs.analyze_path)
if mainargs.verbose:
    print 'XTU uses analyze-cc dir: ' + (analyze_path if analyze_path != ''
                                         else '<taken from PATH>')

analyzer_params = []
if mainargs.enabled_checkers:
    analyzer_params += ['-analyzer-checker', mainargs.enabled_checkers]
if mainargs.disabled_checkers:
    analyzer_params += ['-analyzer-disable-checker', mainargs.disable_checkers]
if not mainargs.no_xtu:
    analyzer_params += ['-analyzer-config',
                        'xtu-dir=' + os.path.abspath(mainargs.xtuindir)]
analyzer_params += ['-analyzer-config', 'reanalyze-xtu-visited=true']
analyzer_params += ['-analyzer-stats']
# analyzer_params += ['-analyzer-output ' + mainargs.output_format]
passthru_analyzer_params = []
for param in analyzer_params:
    passthru_analyzer_params += ['-Xanalyzer']
    passthru_analyzer_params += [param]
passthru_analyzer_params += ['--analyzer-output ' + mainargs.output_format]

analyzer_env = os.environ.copy()
analyzer_env['ANALYZE_BUILD_CLANG'] = os.path.join(clang_path, 'clang')
analyzer_env['ANALYZE_BUILD_REPORT_DIR'] = os.path.abspath(mainargs.xtuoutdir)
analyzer_env['ANALYZE_BUILD_PARAMETERS'] = ' '.join(passthru_analyzer_params)
analyzer_env['ANALYZE_BUILD_REPORT_FORMAT'] = mainargs.output_format
# analyzer_env['ANALYZE_BUILD_VERBOSE'] = 'DEBUG'

graph_lock = threading.Lock()

buildlog_file = open(mainargs.buildlog, 'r')
buildlog = json.load(buildlog_file)
buildlog_file.close()

src_pattern = re.compile('.*\.(C|c|cc|cpp|cxx|ii|m|mm)$', re.IGNORECASE)
dircmd_separator = ': '
dircmd_2_orders = {}
src_build_steps = 0
for step in buildlog:
    if src_pattern.match(step['file']):
        uid = step['directory'] + dircmd_separator + step['command']
        if uid not in dircmd_2_orders:
            dircmd_2_orders[uid] = [src_build_steps]
        else:
            dircmd_2_orders[uid].append(src_build_steps)
        src_build_steps += 1


def get_compiler_and_arguments(cmd):
    had_command = False
    args = []
    for arg in cmd.split():
        if had_command:
            args.append(arg)
        if not had_command and arg.find('=') == -1:
            had_command = True
            compiler = arg
    return compiler, args


def analyze(directory, command):
    compiler, args = get_compiler_and_arguments(command)

    last_src = None
    for cmdpart in command.split():
        if src_pattern.match(cmdpart):
            last_src = cmdpart
    tu_name = ''
    if last_src:
        tu_name += last_src.split(os.sep)[-1]
    tu_name += '_' + str(uuid.uuid4())

    cmdenv = analyzer_env.copy()
    cmdenv['ANALYZE_BUILD_CC'] = compiler
    cmdenv['ANALYZE_BUILD_CXX'] = compiler
    analyze_cmd = os.path.join(analyze_path, 'analyze-cc') + \
        ' ' + string.join(args, ' ')
    if mainargs.verbose:
        print analyze_cmd

    # Buffer output of subprocess and dump it out at the end, so that
    # the subprocess doesn't continue to write output after the user
    # sends SIGTERM
    runOK = True
    out = '******* Error running command'
    try:
        po = subprocess.Popen(analyze_cmd, shell=True,
                              stderr=subprocess.STDOUT,
                              stdout=subprocess.PIPE,
                              universal_newlines=True,
                              cwd=directory,
                              env=cmdenv)
        out, _ = po.communicate()
        runOK = not po.returncode
    except OSError:
        runOK = False
    if mainargs.verbose:
        sys.stdout.write(out)
    if not runOK:
        prefix = os.path.join(os.path.abspath(mainargs.xtuoutdir), "fails")
    else:
        prefix = os.path.join(os.path.abspath(mainargs.xtuoutdir), "passes")
    with open(os.path.join(prefix, "%s.out" % tu_name), "w") as f:
        f.write("%s\n%s" % (analyze_cmd, out))


def analyze_work():
    global concurrent_threads
    global concurrent_thread_times
    global concurrent_thread_last_clock
    global graph_lock
    global dircmd_2_orders
    while len(dircmd_2_orders) > 0:
        graph_lock.acquire()
        found_dircmd_orders = None
        found_dircmd = None
        found_orders = None
        for dircmd_orders in dircmd_2_orders.items():
            dircmd = dircmd_orders[0].split(dircmd_separator, 2)
            orders = dircmd_orders[1]
            assert len(dircmd) == 2 and len(dircmd[0]) > 0 and \
                len(dircmd[1]) > 0
            assert len(orders) > 0
            found_dircmd_orders = dircmd_orders
            found_dircmd = dircmd
            found_orders = orders
            break
        if found_dircmd_orders is not None:
            del dircmd_2_orders[found_dircmd_orders[0]]

            concurrent_thread_current_clock = time.time()
            concurrent_thread_times[concurrent_threads] += \
                concurrent_thread_current_clock - concurrent_thread_last_clock
            concurrent_thread_last_clock = concurrent_thread_current_clock
            concurrent_threads += 1
            if len(concurrent_thread_times) == concurrent_threads:
                concurrent_thread_times.append(0.0)

            graph_lock.release()
            analyze(found_dircmd[0], found_dircmd[1])
            graph_lock.acquire()

            concurrent_thread_current_clock = time.time()
            concurrent_thread_times[concurrent_threads] += \
                concurrent_thread_current_clock - concurrent_thread_last_clock
            concurrent_thread_last_clock = concurrent_thread_current_clock
            concurrent_threads -= 1
            assert concurrent_threads >= 0

            graph_lock.release()
        else:
            graph_lock.release()
            time.sleep(0.125)

try:
    os.makedirs(os.path.abspath(mainargs.xtuoutdir))
except OSError:
    print 'Output directory %s already exists!' % \
        os.path.abspath(mainargs.xtuoutdir)
    sys.exit(1)

os.makedirs(os.path.join(os.path.abspath(mainargs.xtuoutdir), "passes"))
os.makedirs(os.path.join(os.path.abspath(mainargs.xtuoutdir), "fails"))

original_handler = signal.signal(signal.SIGINT, signal.SIG_IGN)
signal.signal(signal.SIGINT, original_handler)

analyze_workers = []
for i in range(int(mainargs.threads)):
    analyze_workers.append(threading.Thread(target=analyze_work))
for worker in analyze_workers:
    worker.start()
try:
    for worker in analyze_workers:
        worker.join(9999999999)
except KeyboardInterrupt:
    exit(1)

try:
    os.removedirs(os.path.abspath(mainargs.xtuoutdir))
    print 'Removing directory %s because it contains no reports' % \
        os.path.abspath(mainargs.xtuoutdir)
except OSError:
    pass

assert concurrent_threads == 0
concurrent_thread_times[0] += time.time() - concurrent_thread_last_clock
sumtime = 0.0
for i in range(len(concurrent_thread_times)):
    sumtime += concurrent_thread_times[i]
print '--- Total running time: %.2fs' % sumtime
for i in range(len(concurrent_thread_times)):
    print '----- ' + \
        (('using %d processes' % i) if i != 0 else 'self time') + \
        ' for %.2fs (%.0f%%)' % (concurrent_thread_times[i],
                                 concurrent_thread_times[i] * 100.0 / sumtime)
