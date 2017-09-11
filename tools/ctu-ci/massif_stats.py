#!/usr/bin/env python
#This script reads a valgrind massif file and
#prints statistics
import argparse
import re
import sys
import os


def read_file(filename):
    ret=[]
    i=0    
    with open(filename) as f:
        for line in f:
            hit=re.match("(.+)=(.+)",line)
            if hit:
                if (hit.group(1)=="snapshot"):
                    i=int(hit.group(2))
                field=hit.group(1)
                value=hit.group(2)
                try:                    
                    ret[i].update({field:value})
                except IndexError:                                
                    ret.insert(i,{field:value})                        
    return ret
      
def get_maximum(data, field=None):
    max=0
    for snapshot in data:        
        if (field):
            val=float(snapshot[field])
            if (val>max):
                max=val
        else:            
            val=float(snapshot)
            if (max<val):
                max=val
    return max

def get_average(data, field=None):
    av=0
    count=0
    for snapshot in data:
        if (field):
            if (snapshot[field]):
                av+=int(snapshot[field])
                count+=1
        else:
            if (snapshot):
                av+=int(snapshot)
                count+=1
    return (float(av)/float(count))



def cummulate_dir_data(dirname,field,file_operation,dir_operation,print_files = None):    
    files = os.listdir(dirname)
    dir_data=[]    
    for file in files:        
        if (re.match(".+massif",file)):
            fullpath=os.path.join(dirname,file)            
            dat=read_file(fullpath)        
            d=file_operation(dat,field)
            if (print_files):
                print(fullpath + ": " + str(int(d))) + " bytes"                
            dir_data.append(d)            
    return dir_operation(dir_data)
    

parser = argparse.ArgumentParser(
            description='Prints valgrind massif (memory profiling) statistics')
parser.add_argument('-p', required=False, action="store_true",
                    dest='print_files',
                    help='Print file level statistics')
parser.add_argument('-m', required=False, action="store_true",
                    dest='file_maximum',
                    help='File level operation: peak heap usage')
parser.add_argument('-M', required=False, action="store_true",
                    dest='dir_maximum',
                    help='Directory level operation: maximum of file level operations')
parser.add_argument('-a', required=False, action="store_true",
                    dest='file_average',
                    help='File level operation: average heap usage')
parser.add_argument('-A', required=False, action="store_true",
                    dest='dir_average',
                    help='Directory level operation: average of file level operations')
parser.add_argument('-f', required=False, metavar='massif-file',
                    dest='massif_file',
                    help='massif file to analyze')

parser.add_argument('-d', required=False, metavar='massif-directory',
                    dest='massif_dir',
                    help='directory with .massif files to analyze')
mainargs = parser.parse_args()

if (not mainargs.massif_file and not mainargs.massif_dir):
    print "ERROR: Either massif file or directory must be specified."
    sys.exit(1)

if mainargs.file_maximum and mainargs.massif_file:
    #Print file level maximum
    data=read_file(mainargs.massif_file)
    print str(get_maximum(data[0],"mem_heap_B"))
    sys.exit (0)
    
if  mainargs.massif_dir:
    #print directory level maximum of file level maximums    
    if (mainargs.file_maximum):
        file_op=get_maximum
    if (mainargs.file_average):
        file_op=get_average
    if (mainargs.dir_maximum):
        dir_op=get_maximum
    if (mainargs.dir_average):
        dir_op=get_average                
    cum=cummulate_dir_data(mainargs.massif_dir,"mem_heap_B",file_op,dir_op,mainargs.print_files)
    print str(int(cum))
