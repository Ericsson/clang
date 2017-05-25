#!/usr/bin/env python

import argparse
import json
import logging
import multiprocessing
import os
import re
import signal
import subprocess
import threading
import time


SOURCE_PATTERN = re.compile('.*\.(C|c|cc|cpp|cxx|ii|m|mm)$', re.IGNORECASE)
DIRCMD_SEPARATOR = ': '


def get_args():
    analyser_output_formats = ['plist-multi-file', 'plist', 'plist-html',
                               'html', 'text']
    analyser_output_format = analyser_output_formats[0]
    parser = argparse.ArgumentParser(
        description='Executes 2nd pass of CTU analysis where we do the '
                    'static analysis taking all cross calls between '
                    'translation units into account',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-b', required=True, dest='buildlog',
                        metavar='build.json',
                        help='JSON Compilation Database to be used')
    parser.add_argument('-p', metavar='preanalyze-dir', dest='ctuindir',
                        help='Use directory for reading preanalyzation data ',
                        default='.ctu')
    parser.add_argument('-o', metavar='output-dir', dest='ctuoutdir',
                        help='Target directory for analyzation results',
                        default='.ctu-out')
    parser.add_argument('-e', metavar='enabled-checker', nargs='+',
                        dest='enabled_checkers',
                        help='List all enabled checkers')
    parser.add_argument('-d', metavar='disabled-checker', nargs='+',
                        dest='disabled_checkers',
                        help='List all disabled checkers')
    parser.add_argument('-j', metavar='threads', dest='threads', type=int,
                        help='Number of threads to be used',
                        default=int(multiprocessing.cpu_count() * 1.0))
    parser.add_argument('-v', dest='verbose', action='store_true',
                        help='Verbose output')
    parser.add_argument('--clang-path', metavar='clang-path',
                        dest='clang_path',
                        help='Set path to directory of clang binaries used '
                             '(default taken from CLANG_PATH envvar)',
                        default=os.environ.get('CLANG_PATH'))
    parser.add_argument('--analyze-cc-path', metavar='analyze-cc-path',
                        dest='analyze_path',
                        help='Set path to directory of analyze-cc used '
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
    parser.add_argument('--no-ctu', dest='no_ctu', action='store_true',
                        help='Do not use CTU at all, '
                             'only do normal static analysis')
    mainargs = parser.parse_args()

    if mainargs.verbose:
        logging.getLogger().setLevel(logging.INFO)

    mainargs.ctuindir = os.path.abspath(mainargs.ctuindir)
    mainargs.ctuoutdir = os.path.abspath(mainargs.ctuoutdir)

    if mainargs.clang_path is None:
        clang_path = ''
    else:
        clang_path = os.path.abspath(mainargs.clang_path)
    logging.info('CTU uses clang dir: ' +
                 (clang_path if clang_path != '' else '<taken from PATH>'))

    if mainargs.analyze_path is None:
        analyze_path = ''
    else:
        analyze_path = os.path.abspath(mainargs.analyze_path)
    logging.info('CTU uses analyze-cc dir: ' +
                 (analyze_path if analyze_path != '' else '<taken from PATH>'))

    return mainargs, clang_path, analyze_path


def get_analyzer_env(mainargs, clang_path):
    analyzer_params = []
    if mainargs.enabled_checkers:
        analyzer_params.append('-analyzer-checker')
        analyzer_params.append(",".join(mainargs.enabled_checkers))
    if mainargs.disabled_checkers:
        analyzer_params.append('-analyzer-disable-checker')
        analyzer_params.append(",".join(mainargs.disabled_checkers))
    if not mainargs.no_ctu:
        analyzer_params.append('-analyzer-config')
        analyzer_params.append('ctu-dir=' + mainargs.ctuindir)
    analyzer_params.append('-analyzer-config')
    analyzer_params.append('reanalyze-ctu-visited=true')
    analyzer_params.append('-analyzer-stats')
    passthru_analyzer_params = []
    for param in analyzer_params:
        passthru_analyzer_params.append('-Xanalyzer')
        passthru_analyzer_params.append(param)
    passthru_analyzer_params.append('--analyzer-output')
    passthru_analyzer_params.append(mainargs.output_format)
    analyzer_env = os.environ.copy()
    analyzer_env['ANALYZE_BUILD_CLANG'] = os.path.join(clang_path, 'clang')
    analyzer_env['ANALYZE_BUILD_REPORT_DIR'] = mainargs.ctuoutdir
    analyzer_env['ANALYZE_BUILD_REPORT_FORMAT'] = mainargs.output_format
    analyzer_env['ANALYZE_BUILD_REPORT_FAILURES'] = 'yes'
    if mainargs.verbose:
        analyzer_env['ANALYZE_BUILD_FORCE_DEBUG'] = 'yes'
    analyzer_env['ANALYZE_BUILD_PARAMETERS'] = \
        ' '.join(passthru_analyzer_params)
    return analyzer_env


def process_buildlog(buildlog_filename):
    with open(buildlog_filename, 'r') as buildlog_file:
        buildlog = json.load(buildlog_file)
    dircmd_2_orders = {}
    src_build_steps = 0
    for step in buildlog:
        if SOURCE_PATTERN.match(step['file']):
            uid = step['directory'] + DIRCMD_SEPARATOR + step['command']
            if uid not in dircmd_2_orders:
                dircmd_2_orders[uid] = [src_build_steps]
            else:
                dircmd_2_orders[uid].append(src_build_steps)
            src_build_steps += 1
    return dircmd_2_orders


def prepare_workspace(mainargs):
    try:
        os.makedirs(mainargs.ctuoutdir)
    except OSError:
        logging.warning('Output directory %s already exists!',
                        mainargs.ctuoutdir)
        exit(1)


def clean_up_workspace(mainargs):
    try:
        os.removedirs(mainargs.ctuoutdir)
        logging.info('Removing directory %s because it contains no reports',
                     mainargs.ctuoutdir)
    except OSError:
        pass


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


def analyze(mainargs, analyze_path, analyzer_env, directory, command):
    compiler, args = get_compiler_and_arguments(command)
    cmdenv = analyzer_env.copy()
    cmdenv['INTERCEPT_BUILD'] = json.dumps({
        'verbose': 10 if mainargs.verbose else 1,
        'cc': [compiler],
        'cxx': [compiler]
        })
    analyze_cmd_str = os.path.join(analyze_path, 'analyze-cc') + ' ' + \
        ' '.join(args)
    logging.info(analyze_cmd_str)

    # Buffer output of subprocess and dump it out at the end, so that
    # the subprocess doesn't continue to write output after the user
    # sends SIGTERM
    run_ok = True
    out = '******* Error running command'
    try:
        popen = subprocess.Popen(analyze_cmd_str, shell=True,
                                 stderr=subprocess.STDOUT,
                                 stdout=subprocess.PIPE,
                                 universal_newlines=True,
                                 cwd=directory,
                                 env=cmdenv)
        out, _ = popen.communicate()
        run_ok = not popen.returncode
    except OSError:
        run_ok = False
    logging.info('Compile status: %s', 'ok' if run_ok else 'failed')
    logging.info(out)


def analyze_worker(mainargs, analyze_path, analyzer_env, lock,
                   dircmd_2_orders):
    while len(dircmd_2_orders) > 0:
        lock.acquire()
        if len(dircmd_2_orders) > 0:
            dircmd, orders = dircmd_2_orders.popitem()
            lock.release()
            dircmdsplit = dircmd.split(DIRCMD_SEPARATOR, 2)
            assert len(dircmdsplit) == 2 and len(dircmdsplit[0]) > 0 and \
                len(dircmdsplit[1]) > 0
            assert len(orders) > 0
            directory = dircmdsplit[0]
            command = dircmdsplit[1]
            analyze(mainargs, analyze_path, analyzer_env, directory, command)
        else:
            lock.release()
            time.sleep(0.125)


def run_parallel(mainargs, analyze_path, analyzer_env, dircmd_2_orders):
    original_handler = signal.signal(signal.SIGINT, signal.SIG_IGN)
    signal.signal(signal.SIGINT, original_handler)
    lock = threading.Lock()
    workers = []
    for _ in range(mainargs.threads):
        workers.append(threading.Thread(target=analyze_worker,
                                        args=(mainargs, analyze_path,
                                              analyzer_env, lock,
                                              dircmd_2_orders)))
    for worker in workers:
        worker.start()
    try:
        for worker in workers:
            worker.join(9999999999)
    except KeyboardInterrupt:
        exit(1)


def main():
    mainargs, clang_path, analyze_path = get_args()
    analyzer_env = get_analyzer_env(mainargs, clang_path)
    dircmd_2_orders = process_buildlog(mainargs.buildlog)
    prepare_workspace(mainargs)
    run_parallel(mainargs, analyze_path, analyzer_env, dircmd_2_orders)
    clean_up_workspace(mainargs)


if __name__ == "__main__":
    main()
