#!/usr/bin/python

import commands
import json
import argparse

parser = argparse.ArgumentParser(description='Returns CodeChecker run id for a run name')

parser.add_argument('--host', type=str, dest="host",
                        default="localhost", required=True,
			help='Codechecker server host')
parser.add_argument('--port', type=str, dest="port",
                        default="8080", required=True,
			help='Codechecker server port')
parser.add_argument('-n', type=str, dest="runname",
                        default="", required=True,
			help='Run name')
parser.add_argument('-p', type=str, dest="prop",
                        default="runId", required=False,
			help='Property to get. resultCount,runDate,runCmd,runId')
#if not args.prop:
#    args.prop="runId"


args = parser.parse_args()

cmd="CodeChecker cmd runs --host "+ args.host +"  -p "+args.port+ " -o json"
#print "executing command"+cmd
out=commands.getoutput(cmd)
#print "output:"+out
runs=json.loads(out)
for run in runs:
    if args.runname in run:
	print str(run[args.runname][args.prop])
    






