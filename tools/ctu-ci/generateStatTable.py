from collections import OrderedDict
import os
import sys
import re
import shutil
#import collections


projectdata = {}
stathtml = open('statistics.html', 'w')
isXtuStat = False
projectname = ""
first = True


def dump():
    global projectname, projectdata, stathtml
    stathtml.write("<h1>" + projectname + "</h1>\n")
    stathtml.write("<table>\n")
    stathtml.write("<tr>\n")
    stathtml.write("<th>StatName</th><th>noXTU</th><th>XTU</th>")
    stathtml.write("</tr>\n")
    od = OrderedDict(sorted(projectdata.items()))
    for cat, stats in od.iteritems():
        stathtml.write("<tr>\n")
        stathtml.write("<td>" + cat + "</td>")
        stathtml.write("<td>" + stats[0] + "</td>")
        stathtml.write("<td>" + stats[1] + "</td>")
        stathtml.write("</tr>\n")
    stathtml.write("</table>\n\n")


stathtml.write("<html><head><title>Current XTU Detailed Statistics</title></head>"
"<style>table {border-collapse: collapse; border-spacing: 0;} td, th {border: 1px solid #999999;} th {background: #dddddd; text-align: center;} td {text-align: right;}"
" td:first-child {text-align: left;} tr:nth-child(even) td {background: #ffffff;} tr:nth-child(odd) td {background: #eeeeee;}</style>"
"<body><p><a href=http://cc.elte.hu:8080/>See results in CodeChecker here!</a></p>")

with open(sys.argv[1]) as f:
    content = f.readlines()
    for line in content:
        if line == "XTU\n":
            isXtuStat = True
        elif line == "noXTU\n":
            isXtuStat = False
        elif line[0] == 'P':
            if first:
                first = False
            else:
                dump()
            projectname = line
            projectdata = {}
        else:
            stat = line.split("-")
            if not projectdata.has_key(stat[1]):
                projectdata[stat[1]] = [None]*2
            if isXtuStat:
                projectdata[stat[1]][1] = stat[0]
            else:
                projectdata[stat[1]][0] = stat[0]
dump()
stathtml.close()
shutil.move("./statistics.html", "/var/www/html/clang-ctu/detailed_stats.html")



