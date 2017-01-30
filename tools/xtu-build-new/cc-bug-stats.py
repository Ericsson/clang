#!/usr/bin/env python
# -------------------------------------------------------------------------
#                     The CodeChecker Infrastructure
#   This file is distributed under the University of Illinois Open Source
#   License. See LICENSE.TXT for details.
# -------------------------------------------------------------------------

import argparse
import json
import math
import os
import subprocess
import sys

from time import sleep

try:
    from codechecker_lib.util import call_command
except ImportError:
    print("WARNING! Couldn't import CodeChecker library.")
    print("Check path please...")

    def call_command(command):
        try:
            out = subprocess.check_output(command,
                                          bufsize=-1,
                                          stderr=subprocess.STDOUT)
            return out, 0
        except subprocess.CalledProcessError as ex:
            return ex.output, ex.returncode


# Check if CodeChecker exists

try:
    with open(os.devnull, 'w') as nullfile:
        r = subprocess.call(["CodeChecker"], stderr=nullfile, stdout=nullfile)

    if r != 2:
        print("CodeChecker couldn't import some modules properly!")
        print("Check path please...")
        sys.exit(1)
except OSError:
    print("`CodeChecker` cannot be called!")
    print("Check path please...")
    sys.exit(1)


##############################################################################

parser = argparse.ArgumentParser(
    prog='BugStats',
    description='''BugStats can print BugPath statistics from CodeChecker
results.'''
)

parser.add_argument('--host', type=str, dest="host",
                    default='localhost',
                    help='Server host.')
parser.add_argument('--port', type=str, dest="port",
                    default=8001,
                    required=True, help='HTTP Server port.')

name_group = parser.add_mutually_exclusive_group(required=True)
name_group.add_argument('-n', '--name', nargs='+', type=str, dest="names",
                        help='Runs to include in the output '
                             '(single full run names)')
name_group.add_argument('-p', '--project', type=str, dest="project",
                        help='Project group (one XTU and one non-XTU) '
                             'to handle')
name_group.add_argument('-a', '--all', action='store_true',
                        dest="all",
                        help='Calculate statistics for ALL project found '
                             'on the server.')

parser.add_argument('--no-histogram',
                    action='store_false',
                    dest="histogram",
                    help='Disable histogram generation. '
                         'Histogram generation requires the `data_hacks` '
                         'pip module.')

args = parser.parse_args()

##############################################################################

# Check if histogram module exists
Histogram = False
if args.histogram:
    try:
        with open(os.devnull, 'w') as nullfile:
            r = subprocess.call(["histogram.py"],
                                stderr=nullfile,
                                stdout=nullfile)

        if r == 1:
            Histogram = True
    except OSError:
        # Histogram generation remains disabled.
        pass

    # if not Histogram:
    #     print("WARNING! `histogram.py` not found --- "
    #           "not generating histograms.")
    #     print("To enable, please `pip install data_hacks`.")
    #     print("To squelch this error, please specify '--no-histogram'.")
    #     print("\n\n")
    #     sleep(1)

##############################################################################

_CodeCheckerSharedArgs = ["--host", args.host, "--port", args.port]


def cc_command_builder(cmds, extra_args=[]):
    return ["CodeChecker"] + cmds + extra_args + _CodeCheckerSharedArgs +\
           ["-o", "json"]

# Check if the projects exist
valid_projects_on_server, _ = call_command(
    cc_command_builder(["cmd", "runs"]))

if 'Connection refused' in valid_projects_on_server or \
        'Name or service not known' in valid_projects_on_server:
    print("ERROR! Couldn't connect to server.")
    sys.exit(1)

try:
    valid_projects_on_server = json.loads(valid_projects_on_server)
except ValueError:
    print("ERROR! CodeChecker didn't return proper JSON?! (valid projects)")
    sys.exit(1)

existing_runs = [p.keys()[0] for p in valid_projects_on_server]

if args.names:
    print("Getting result metrics for " + ', '.join(args.names))

    project_names = [p for p in args.names if p in existing_runs]
    nonexistent = [p for p in args.names if p not in existing_runs]

    if len(nonexistent) > 0:
        print("WARNING! Ignoring specified but NON-EXISTENT runs: " +
              ', '.join(nonexistent))
elif args.project:
    print("Getting result metrics for project " + args.project)

    # Search for valid project names
    candidates = set([project for project in existing_runs
                      if args.project in project])
    xtus = [project for project in candidates
            if 'noXTU' not in project and 'noxtu' not in project]
    nonxtus = list(candidates - set(xtus))

    xtus.sort()
    nonxtus.sort()

    if len(xtus) != 1 or len(nonxtus) != 1:
        print("Multiple options available...")
        print("Using newest runs: {0} and {1}".format(
            xtus[len(xtus) - 1],
            nonxtus[len(nonxtus) - 1]
        ))

    project_names = [xtus[len(xtus) - 1], nonxtus[len(nonxtus) - 1]]
elif args.all:
    print("Calculating for every project...")
    project_names = existing_runs

# ----------------------------------------------------


def calculate_metrics(bugPathLengths):
    bugPathLengths.sort()

    num_lengths = float(len(bugPathLengths))
    sum_lengths = float(sum(bugPathLengths))
    mean = sum_lengths / num_lengths

    percentiles_needed = [25, 50, 75, 90]
    percentile_values = []
    for perc in percentiles_needed:
        perc = float(perc) / 100
        idx = (perc * num_lengths) - 1
        middle_avg = not idx.is_integer()

        if middle_avg:
            # idx is NOT a whole number
            # we need to round it up
            idx = math.ceil(idx)

        # the percentile is the indexth element (if idx was rounded...)
        idx = int(idx)  # it is an index!
        percentile = bugPathLengths[idx]

        if not middle_avg:
            # if idx WAS a whole number, the percentile is the average
            # of the indexth element and the element after it
            percentile = float(bugPathLengths[idx] +
                               bugPathLengths[idx + 1]) / 2

        percentile_values.append((int(perc * 100), int(percentile)))

    print('Total # of bugs: ' + str(num_lengths))
    print('MIN BugPath length: ' + str(bugPathLengths[0]))
    print('MAX BugPath length: ' +
          str(bugPathLengths[len(bugPathLengths) - 1]))
    print('Mean length: ' + str(mean))
    for percentile, value in percentile_values:
        print("{0}% percentile: {1}".format(percentile, value))

if Histogram:
    def make_histogram(bug_path_lengths):
        print("\n------------------- Histogram -------------------")
        p = subprocess.Popen(["histogram.py"],
                             stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE,
                             bufsize=1)

        for length in bug_path_lengths:
            p.stdin.write(str(length) + "\n")
        p.stdin.flush()
        print(p.communicate("")[0])

for project in project_names:
    print("###############################################################")
    print("Generating bugpath metrics for '" + project + "'")

    try:
        results, _ = call_command(cc_command_builder(
            ["cmd", "results"], ["--name", project]
        ))
        results = json.loads(results)
    except ValueError:
        print("ERROR! CodeChecker didn't return proper JSON?! (results)")
        continue

    bug_path_lengths = [res['bugPathLength'] for res in results]
    if any([True if bpl is None else False for bpl in bug_path_lengths]):
        print("ERROR! CodeChecker server didn't return new enough ReportData "
              "structure containing BugPathLength counts.")
        print("ERROR: !! OUTDATED SERVER !!")
        sys.exit(2)

    calculate_metrics(bug_path_lengths)

    if Histogram:
        make_histogram(bug_path_lengths)
