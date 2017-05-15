#!/usr/bin/env python

import argparse
import io
import json
import multiprocessing
import os
import re
import subprocess
import string

threading_factor = int(multiprocessing.cpu_count() * 1.5)

parser = argparse.ArgumentParser(description='Executes 1st pass of XTU analysis')
parser.add_argument('-b', required=True, dest='buildlog', metavar='build.json', help='Use a JSON Compilation Database')
parser.add_argument('-p', metavar='preanalyze-dir', dest='xtuindir', help='Use directory for reading preanalyzation data (default=".xtu")', default='.xtu')
parser.add_argument('-j', metavar='threads', dest='threads', help='Number of threads used (default=' + str(threading_factor) + ')', default=threading_factor)
parser.add_argument('-v', dest='verbose', action='store_true', help='Verbose output of every command executed')
parser.add_argument('--clang-path', metavar='clang-path', dest='clang_path', help='Set path of clang binaries to be used (default taken from CLANG_PATH environment variable)', default=os.environ.get('CLANG_PATH', '.'))
mainargs = parser.parse_args()

clang_path = mainargs.clang_path
if not clang_path.endswith('/') :
    clang_path += "/"
if mainargs.verbose :
    print 'Using clang dir: ' + clang_path

buildlog_file = open(mainargs.buildlog, 'r')
buildlog = json.load(buildlog_file)
buildlog_file.close()

src_pattern = re.compile(".*\.(cc|c|cxx|cpp)$", re.IGNORECASE)
src_2_cmd = {}
src_order = []
cmd_2_src = {}
cmd_order = []
for step in buildlog :
    if src_pattern.match(step['file']) :
        if step['file'] not in src_2_cmd :
            src_2_cmd[step['file']] = step['command']
            src_order.append(step['file'])
        if step['command'] not in cmd_2_src :
            cmd_2_src[step['command']] = [step['file']]
            cmd_order.append(step['command'])
        else :
            cmd_2_src[step['command']].append(step['file'])

def clear_file(filename) :
    try :
        os.remove(filename)
    except OSError:
        pass

def get_command_arguments(cmd) :
    had_command = False
    args = []
    for arg in cmd.split() :
        if had_command and not src_pattern.match(arg) :
            args.append(arg)
        if not had_command and arg.find('=') == -1 :
            had_command = True
    return args

def generate_ast(source) :
    cmd = src_2_cmd[source]
    args = get_command_arguments(cmd)
    arch_command = clang_path + 'clang-cmdline-arch-extractor ' + string.join(args, ' ') + ' ' + source
    if mainargs.verbose :
        print arch_command
    arch_output = subprocess.check_output(arch_command, shell=True)
    arch = arch_output[arch_output.rfind('@')+1:].strip()
    ast_path = os.path.join(mainargs.xtuindir, os.path.join("/ast/" + arch, os.path.realpath(source)[1:] + ".ast")[1:])
    try :
        os.makedirs(os.path.dirname(ast_path))
    except OSError:
        if os.path.isdir(os.path.dirname(ast_path)):
            pass
        else :
            raise
    ast_command = clang_path + 'clang -emit-ast ' + string.join(args, ' ') + ' -w ' + source + ' -o ' + ast_path
    if mainargs.verbose :
        print ast_command
    subprocess.call(ast_command, shell=True)

def map_functions(command) :
    args = get_command_arguments(command)
    sources = cmd_2_src[command]
    funcmap_command = clang_path + 'clang-func-mapping --xtu-dir ' + mainargs.xtuindir + ' ' + string.join(sources, ' ') + ' -- ' + string.join(args, ' ')
    if mainargs.verbose :
        print funcmap_command
    subprocess.call(funcmap_command, shell=True)

clear_file(mainargs.xtuindir + '/cfg.txt')
clear_file(mainargs.xtuindir + '/definedFns.txt')
clear_file(mainargs.xtuindir + '/externalFns.txt')

ast_workers = multiprocessing.Pool(processes=mainargs.threads)
for source in src_order :
    ast_workers.apply_async(generate_ast, [source])
ast_workers.close()
ast_workers.join()


funcmap_workers = multiprocessing.Pool(processes=mainargs.threads)
for command in cmd_order :
    funcmap_workers.apply_async(map_functions, [command])
funcmap_workers.close()
funcmap_workers.join()

