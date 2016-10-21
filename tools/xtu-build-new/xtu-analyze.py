#!/usr/bin/env python

import argparse
import io
import json
import multiprocessing
import os
import re
import signal
import subprocess
import string
import sys

timeout = 86400
analyser_output_formats = ['plist', 'plist-multi-file', 'html', 'plist-html', 'text']
analyser_output_format = analyser_output_formats[0]

parser = argparse.ArgumentParser(description='Executes 2nd pass of XTU analysis')
parser.add_argument('-b', required=True, dest='buildlog', metavar='build.json', help='Use a JSON Compilation Database')
#parser.add_argument('-g', dest='buildgraph', metavar='build-graph.json', help='Use a JSON Build Dependency Graph (required in normal mode)')
parser.add_argument('-p', metavar='preanalyze-dir', dest='xtuindir', help='Use directory for reading preanalyzation data (default=".xtu")', default='.xtu')
parser.add_argument('-o', metavar='output-dir', dest='xtuoutdir', help='Use directory for output analyzation results (default=".xtu-out")', default='.xtu-out')
parser.add_argument('-e', metavar='enabled-checker', nargs='+', dest='enabled_checkers', help='List all enabled checkers')
parser.add_argument('-d', metavar='disabled-checker', nargs='+', dest='disabled_checkers', help='List all disabled checkers')
parser.add_argument('-j', metavar='threads', dest='threads', help='Number of threads used (default=1)', default=1)
parser.add_argument('-v', dest='verbose', action='store_true', help='Verbose output of every command executed')
parser.add_argument('--clang-path', metavar='clang-path', dest='clang_path', help='Set path of clang binaries to be used (default taken from CLANG_PATH environment variable)', default=os.environ.get('CLANG_PATH'))
parser.add_argument('--analyze-cc-path', metavar='analyze-cc-path', dest='analyze_path', help='Set path of analyze-cc to be used (default is taken from CLANG_ANALYZE_CC_PATH environment variable)', default=os.environ.get('CLANG_ANALYZE_CC_PATH'))
parser.add_argument('--output-format', metavar='format',
    choices=analyser_output_formats, default=analyser_output_format,
    help='Format for analysis reports (one of %s; default is "%s").' %
    (', '.join(analyser_output_formats), analyser_output_format))
parser.add_argument('--timeout', metavar='N', help='Timeout for analysis in seconds (default: %d)' % timeout, default=timeout)
parser.add_argument('--reanalyze-xtu-visited', dest='without_visitedfns', action='store_true', help='Do not use a buildgraph file and visitedFunc.txt, reanalyze everything in random order with full parallelism (set -j for optimal results)')
mainargs = parser.parse_args()

#if mainargs.without_visitedfns and mainargs.buildgraph is not None :
#    print 'A buildgraph JSON cannot be used when in reanalyze-xtu-visited mode.'
#    sys.exit(1)
#if not mainargs.without_visitedfns and mainargs.buildgraph is None :
#    print 'A buildgraph JSON should be given in normal mode to avoid revisiting functions.'
#    sys.exit(1)

if mainargs.clang_path is None :
    clang_path = ''
else :
    clang_path = os.path.abspath(mainargs.clang_path)
if mainargs.verbose :
    print 'XTU uses clang dir: ' + (clang_path if clang_path != '' else '<taken from PATH>')

if mainargs.analyze_path is None :
    analyze_path = ''
else :
    analyze_path = os.path.abspath(mainargs.analyze_path)
if mainargs.verbose :
    print 'XTU uses analyze-cc dir: ' + (analyze_path if analyze_path != '' else '<taken from PATH>')

analyzer_params = []
if mainargs.enabled_checkers:
    analyzer_params += [ '-analyzer-checker', mainargs.enabled_checkers ]
if mainargs.disabled_checkers:
    analyzer_params += [ '-analyzer-disable-checker', mainargs.disable_checkers ]
analyzer_params += [ '-analyzer-config', 'xtu-dir=' + os.path.abspath(mainargs.xtuindir)]
if mainargs.without_visitedfns :
    analyzer_params += [ '-analyzer-config', 'reanalyze-xtu-visited=true' ]
analyzer_params += [ '-analyzer-stats' ]
analyzer_params += [ '-analyzer-output=' + mainargs.output_format ]
passthru_analyzer_params = []
for param in analyzer_params :
    passthru_analyzer_params += ['-Xanalyzer']
    passthru_analyzer_params += [param]

analyzer_env = {}
analyzer_env['ANALYZE_BUILD_CLANG'] = os.path.join(clang_path, 'clang')
analyzer_env['ANALYZE_BUILD_REPORT_DIR'] = os.path.abspath(mainargs.xtuoutdir)
analyzer_env['ANALYZE_BUILD_PARAMETERS'] = ' '.join(passthru_analyzer_params)
analyzer_env['ANALYZE_BUILD_REPORT_FORMAT'] = mainargs.output_format
#analyzer_env['ANALYZE_BUILD_VERBOSE'] = 'DEBUG'

buildlog_file = open(mainargs.buildlog, 'r')
buildlog = json.load(buildlog_file)
buildlog_file.close()

#buildgraph_file = open(mainargs.buildgraph, 'r')
#buildgraph = json.load(buildgraph_file)
#buildgraph_file.close()

src_pattern = re.compile('.*\.(cc|c|cxx|cpp)$', re.IGNORECASE)
cmd_2_order = {}
build_steps = 0
for step in buildlog :
    if src_pattern.match(step['file']) :
        cmd_2_order[step['command']] = build_steps
    build_steps += 1

def get_compiler_and_arguments(cmd) :
    had_command = False
    args = []
    for arg in cmd.split() :
        if had_command :
            args.append(arg)
        if not had_command and arg.find('=') == -1 :
            had_command = True
            compiler = arg
    return compiler, args

def analyze((directory, command)) :
    old_environ = os.environ
    old_workdir = os.getcwd()
    compiler, args = get_compiler_and_arguments(command)
    os.environ.update(analyzer_env)
    os.environ['ANALYZE_BUILD_CC'] = compiler
    os.environ['ANALYZE_BUILD_CXX'] = compiler
    os.chdir(directory)
    analyze_cmd = os.path.join(analyze_path, 'analyze-cc') + ' ' + string.join(args, ' ')
    if mainargs.verbose :
        print analyze_cmd
    # Buffer output of subprocess and dump it out at the end, so that
    # the subprocess doesn't continue to write output after the user
    # sends SIGTERM
    po = subprocess.Popen(analyze_cmd, shell=True, stderr=subprocess.PIPE,
            stdout=subprocess.PIPE)
    out, err = po.communicate()
    sys.stderr.write(err)
    sys.stdout.write(out)
    os.chdir(old_workdir)
    os.environ.update(old_environ)

try:
    os.makedirs(os.path.abspath(mainargs.xtuoutdir))
except OSError:
    print 'Output directory %s already exists!' % os.path.abspath(mainargs.xtuoutdir)
    sys.exit(1)

original_handler = signal.signal(signal.SIGINT, signal.SIG_IGN)
analyze_workers = multiprocessing.Pool(processes=mainargs.threads)
signal.signal(signal.SIGINT, original_handler)
steps = [(step['directory'], step['command']) for step in buildlog
        if step['command'] in cmd_2_order]
try:
    res = analyze_workers.map_async(analyze, steps)
    # Block with timeout so that signals are not ignored, python bug 8296
    res.get(mainargs.timeout)
except KeyboardInterrupt:
    analyze_workers.terminate()
    analyze_workers.join()
    exit(1)
else:
    analyze_workers.close()
    analyze_workers.join()

#TODO correct execution order

os.system('rm -vf ' + os.path.join(os.path.abspath(mainargs.xtuindir), 'visitedFunc.txt'))
try:
    os.removedirs(os.path.abspath(mainargs.xtuoutdir))
    print 'Removing directory %s because it contains no reports' % os.path.abspath(mainargs.xtuoutdir)
except OSError:
    pass

