#!/usr/bin/env python

import argparse
import json
import glob
import logging
import multiprocessing
import os
import re
import signal
import subprocess
import shlex
import shutil
import tempfile

SOURCE_PATTERN = re.compile('.*\.(C|c|cc|cpp|cxx|ii|m|mm)$', re.IGNORECASE)
TIMEOUT = 86400
EXTERNAL_FUNCTION_MAP_FILENAME = 'externalFnMap.txt'
TEMP_EXTERNAL_FNMAP_FOLDER = 'tmpExternalFnMaps'


def get_args():
    parser = argparse.ArgumentParser(
        description='Executes 1st pass of CTU analysis where we preprocess '
                    'all files in the compilation database and generate '
                    'AST dumps and other necessary information from those '
                    'to be used later by the 2nd pass of '
                    'Cross Translation Unit analysis',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-b', required=True, dest='buildlog',
                        metavar='build.json',
                        help='JSON Compilation Database to be used')
    parser.add_argument('-p', metavar='preanalyze-dir', dest='ctuindir',
                        help='Target directory for preanalyzation data',
                        default='.ctu')
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
    mainargs = parser.parse_args()

    if mainargs.verbose:
        logging.getLogger().setLevel(logging.INFO)

    if mainargs.clang_path is None:
        clang_path = ''
    else:
        clang_path = os.path.abspath(mainargs.clang_path)
    logging.info('CTU uses clang dir: ' +
                 (clang_path if clang_path != '' else '<taken from PATH>'))

    return mainargs, clang_path


def process_buildlog(buildlog_filename, src_2_dir, src_2_cmd, src_order,
                     cmd_2_src, cmd_order):
    with open(buildlog_filename, 'r') as buildlog_file:
        buildlog = json.load(buildlog_file)
    for step in buildlog:
        if SOURCE_PATTERN.match(step['file']):
            if step['file'] not in src_2_dir:
                src_2_dir[step['file']] = step['directory']
                src_2_cmd[step['file']] = step['command']
                src_order.append(step['file'])
            if step['command'] not in cmd_2_src:
                cmd_2_src[step['command']] = [step['file']]
                cmd_order.append(step['command'])
            else:
                cmd_2_src[step['command']].append(step['file'])


def clear_file(filename):
    try:
        os.remove(filename)
    except OSError:
        pass


def init_temp_stuff(ctuindir):
    os.mkdir(os.path.join(ctuindir, TEMP_EXTERNAL_FNMAP_FOLDER))


def clear_temp_stuff(ctuindir):
    shutil.rmtree(os.path.join(ctuindir, TEMP_EXTERNAL_FNMAP_FOLDER),
                  ignore_errors=True)


def clear_workspace(ctuindir):
    clear_temp_stuff(ctuindir)
    clear_file(os.path.join(ctuindir, EXTERNAL_FUNCTION_MAP_FILENAME))


def get_command_arguments(cmd):
    had_command = False
    args = []
    for arg in cmd.split():
        if had_command and not SOURCE_PATTERN.match(arg):
            args.append(arg)
        if not had_command and arg.find('=') == -1:
            had_command = True
    return args


def get_triple_arch(clang_path, clang_args, source):
    """Returns the architecture part of the target triple in a compilation
    command. """
    arch = ""
    clang_cmd = [os.path.join(clang_path, 'clang'), "-###"]
    clang_cmd.extend(clang_args)
    clang_cmd.append(source)
    clang_out = subprocess.check_output(clang_cmd, stderr=subprocess.STDOUT,
                                        shell=False)
    clang_params = shlex.split(clang_out)
    i = 0
    while i < len(clang_params) and clang_params[i] != "-triple":
        i += 1
    if i < (len(clang_params) - 1):
        arch = clang_params[i + 1].split("-")[0]
    return arch


def generate_ast(params):
    source, command, directory, clang_path, ctuindir = params
    args = get_command_arguments(command)
    arch = get_triple_arch(clang_path, args, source)
    ast_joined_path = os.path.join(ctuindir, 'ast', arch,
                                   os.path.realpath(source)[1:] + '.ast')
    ast_path = os.path.abspath(ast_joined_path)
    try:
        os.makedirs(os.path.dirname(ast_path))
    except OSError:
        if os.path.isdir(os.path.dirname(ast_path)):
            pass
        else:
            raise
    dir_command = ['cd', directory]
    ast_command = [os.path.join(clang_path, 'clang'), '-emit-ast']
    ast_command.extend(args)
    ast_command.append('-w')
    ast_command.append(source)
    ast_command.append('-o')
    ast_command.append(ast_path)
    ast_command_str = ' '.join(dir_command) + " && " + ' '.join(ast_command)
    logging.info(ast_command_str)
    subprocess.call(ast_command_str, shell=True)


def map_functions(params):
    command, sources, directory, clang_path, ctuindir = params
    args = get_command_arguments(command)
    logging.info("map_functions command: " + command)
    logging.info("sources: " + ' '.join(sources))
    arch = get_triple_arch(clang_path, args, sources[0])
    dir_command = ['cd', directory]
    funcmap_command = [os.path.join(clang_path, 'clang-func-mapping')]
    funcmap_command.extend(sources)
    funcmap_command.append('--')
    funcmap_command.extend(args)
    funcmap_command_str = ' '.join(dir_command) + \
                          " && " + ' '.join(funcmap_command)
    logging.info("Calling function map: " + funcmap_command_str)
    output = []
    fn_out = subprocess.check_output(funcmap_command_str, shell=True)
    fn_list = fn_out.splitlines()
    for fn_txt in fn_list:
        dpos = fn_txt.find(" ")
        mangled_name = fn_txt[0:dpos]
        path = fn_txt[dpos + 1:]
        ast_path = os.path.join("ast", arch, path[1:] + ".ast")
        output.append(mangled_name + "@" + arch + " " + ast_path)
    extern_fns_map_folder = os.path.join(ctuindir,
                                         TEMP_EXTERNAL_FNMAP_FOLDER)
    logging.info("functionmap: " + ' '.join(output))
    if output:
        with tempfile.NamedTemporaryFile(dir=extern_fns_map_folder,
                                         delete=False) as out_file:
            out_file.write("\n".join(output) + "\n")


def merge_external_fn_maps(ctuindir):
    files = glob.glob(os.path.join(ctuindir, TEMP_EXTERNAL_FNMAP_FOLDER,
                                   '*'))
    extern_fns_map_file = os.path.join(ctuindir,
                                       EXTERNAL_FUNCTION_MAP_FILENAME)
    with open(extern_fns_map_file, 'wb') as out_file:
        for filename in files:
            with open(filename, 'rb') as in_file:
                shutil.copyfileobj(in_file, out_file)


def run_parallel(threads, workfunc, funcparams):
    original_handler = signal.signal(signal.SIGINT, signal.SIG_IGN)
    workers = multiprocessing.Pool(processes=threads)
    signal.signal(signal.SIGINT, original_handler)
    try:
        # Block with timeout so that signals don't get ignored, python bug 8296
        workers.map_async(workfunc, funcparams).get(TIMEOUT)
    except KeyboardInterrupt:
        workers.terminate()
        workers.join()
        exit(1)
    else:
        workers.close()
        workers.join()


def main():
    mainargs, clang_path = get_args()
    clear_workspace(mainargs.ctuindir)

    src_2_dir = {}
    src_2_cmd = {}
    src_order = []
    cmd_2_src = {}
    cmd_order = []
    process_buildlog(mainargs.buildlog, src_2_dir, src_2_cmd, src_order,
                     cmd_2_src, cmd_order)

    run_parallel(mainargs.threads, generate_ast,
                 [(src, src_2_cmd[src], src_2_dir[src], clang_path,
                   mainargs.ctuindir) for src in src_order])

    init_temp_stuff(mainargs.ctuindir)
    run_parallel(mainargs.threads, map_functions,
                 [(cmd, cmd_2_src[cmd], src_2_dir[cmd_2_src[cmd][0]],
                   clang_path, mainargs.ctuindir) for cmd in cmd_order])

    merge_external_fn_maps(mainargs.ctuindir)
    clear_temp_stuff(mainargs.ctuindir)


if __name__ == "__main__":
    main()
