#!/bin/dash


if [ ! -z $1 ] && ( [ $1 = "-h" ] || [ $1 = "--help" ] ); then
  echo "Usage: "$0" [html output dir] [web-root] [codechecker port] [stat_file]"
  echo "Creates clang measurement result summary in the <html output dir>"
  echo "By default $0 /var/www/html/clang-ctu/experiments /clang-ctu/experiments 15002 all_statistics_exp.txt"
  exit 0
fi



HTML_OUT_DIR=/var/www/html/clang-ctu/experiments
WEB_ROOT=/clang-ctu/experiments
CODECHECKER_PORT=15002
STATFILE=all_statistics_exp.txt
if [ ! -z $1 ]; then
    HTML_OUT_DIR=$1
fi

if [ ! -z $2 ]; then
  WEB_ROOT=$2
fi

if [ ! -z $3 ] ; then
    CODECHECKER_PORT=$3
fi

if [ ! -z $4 ] ; then
    STATFILE=$4
fi
echo "Recreating $HTML_OUT_DIR/index.html"

export PATH=./clang_build/CodeChecker/bin:$PATH

echo "<html><head><title>Single TU vs Cross TU Clang Static Analysis Results Comparison</title></head>" >index.html


echo "<style>table {border-collapse: collapse; border-spacing: 0;} td, th {border: 1px solid #999999;} th {background: #dddddd; text-align: center;} td {text-align: center;} td:first-child {text-align: left;} tr:nth-child(even) td {background: #ffffff;} tr:nth-child(odd) td {background: #eeeeee;}</style>" >>index.html
echo "<h1>Single TU vs Cross TU Clang Static Analysis Results Comparison</h1>" >> index.html

cat ./intro.html >>index.html

echo "<table><caption></caption>" >>index.html
echo "<tr><th>Analyzed Project</th>" >>index.html
echo "<th>Number of single TU findings</th><th>Number of CTU findings</th><th>New findings</th><th>Total files in project</th><th>Files CTU successfully analyzed</th><th>Files CTU failed</th>" >> index.html
echo "<th>Time of singleTU (sec)</th><th>Time of CTU build (1st pass) (sec)</th>" >> index.html
echo "<th>Time of CTU analysis (2nd pass) (sec)</th>" >> index.html
echo "<th>Max heap usage of single TU (B)</th><th>Max Heap usage of CTU (B)</th>" >> index.html
echo "<th>Analysis Coverage</th>" >> index.html
echo "</tr>" >>index.html
cat $STATFILE | while read LINE ; do
  PROJECT=$(echo $LINE | cut -d" " -f1)
  RUNID_XTU=$(./get_run_prop_by_name.py --host localhost --port $CODECHECKER_PORT -n "$PROJECT"_XTU)
  RUNID_noXTU=$(./get_run_prop_by_name.py --host localhost --port $CODECHECKER_PORT -n "$PROJECT"_noXTU)
  RESCOUNT_noXTU=$(./get_run_prop_by_name.py --host localhost --port $CODECHECKER_PORT -n "$PROJECT"_noXTU -p resultCount)
  RESCOUNT_XTU=$(./get_run_prop_by_name.py --host localhost --port $CODECHECKER_PORT -n "$PROJECT"_XTU -p resultCount)
  echo "runid:"$RUNID_XTU
  echo "<tr>" >>index.html
  echo "<td>"$(echo $LINE | cut -d" " -f1)"</td>" >>index.html
  echo "<td><a href=\"http://cc.elte.hu:$CODECHECKER_PORT/#run=$RUNID_noXTU\">"$RESCOUNT_noXTU"</a></td>" >>index.html
  echo "<td><a href=\"http://cc.elte.hu:$CODECHECKER_PORT/#run=$RUNID_XTU\">"$RESCOUNT_XTU"</a></td>" >>index.html
  echo "<td><a href=\"http://cc.elte.hu:$CODECHECKER_PORT/#baseline="$RUNID_noXTU"&newcheck="$RUNID_XTU"\">"new findings"</a></td>" >>index.html
  echo "<td>"$(echo $LINE | cut -d" " -f2)"</td>" >>index.html
  echo "<td>"$(echo $LINE | cut -d" " -f3)"</td>" >>index.html
  echo "<td>"$(echo $LINE | cut -d" " -f4)"</td>" >>index.html
  echo "<td>"$(echo $LINE | cut -d" " -f6)"</td>" >>index.html
  echo "<td>"$(echo $LINE | cut -d" " -f7)"</td>" >>index.html
  echo "<td>"$(echo $LINE | cut -d" " -f5)"</td>" >>index.html
  echo "<td><a href=$WEB_ROOT/""$PROJECT""_NoXTU_heap_usage.txt>"$(echo $LINE | cut -d" " -f8)"</a></td>" >>index.html
  echo "<td><a href=$WEB_ROOT/""$PROJECT""_XTU_heap_usage.txt>"$(echo $LINE | cut -d" " -f9)"</a></td>" >>index.html
  echo "<td><a href=$WEB_ROOT/""$PROJECT""_gcovNoXtu/coverage.html>Single TU Coverage</a><br><a href=$WEB_ROOT/""$PROJECT""_gcovXtu/coverage.html>CTU Coverage</a><br><a href=$WEB_ROOT/""$PROJECT""_gcovDiff/coverage.html>Diff Coverage</a></td>" >>index.html
  echo "</tr>" >>index.html
done
echo "</table></body></html>" >>index.html
echo "<body><p><a href=http://cc.elte.hu:$CODECHECKER_PORT/>See all results in CodeChecker here!</a></p>" >>index.html
echo "<p><a href=http://cc.elte.hu/clang-ctu/detailed_stats.html>See more detailed statistics here!</a></p>" >>index.html
mv -f index.html $HTML_OUT_DIR
