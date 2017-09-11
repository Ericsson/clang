#!/bin/bash

if [ ! -z $1 ] && ( [ $1 = "-h" ] || [ $1 = "--help" ] ) ; then
  echo "Usage: "$0" [--memprof]"
  echo "--memprof creates valgrind massif memory profile"
  echo "--production publishes run results publicly (/clang-ctu) page. By default publishes in /clang-ctu/experiments"
  echo "--reparse Do not create AST dumps. Analyze on the fly"
  echo "--use-usr Use USR for function identification instead of mangled name."
  echo "--name Run name extension"
  exit 0
fi



cd clang_build/clang/
git pull --rebase
cd ../buildrelwdeb/
ninja -j16
cd ../../

. clang_build/codechecker/venv/bin/activate
cd projects
rm statistics.txt
rm detailed_stats.txt

export PATH=/mnt/storage/xtu-service/clang_build/buildrelwdeb/bin:/mnt/storage/xtu-service/clang_build/codechecker/build/CodeChecker/bin:$PATH
MEMPROF=false
HTML_DIR="/var/www/html/clang-ctu/experiments"
WEB_ROOT="/clang-ctu/experiments"
CODECHECKER_PORT="15002"
MODE="exp" #exp or prod
REPARSE=""
USR=""
i=0
j=0
NAME=""
for var in "$@"
do
  if  [ "$var" = "--memprof" ]; then
    MEMPROF=true
  fi
  if  [ "$var" = "--reparse" ]; then
    REPARSE="--reparse"
  fi
  if  [ "$var" = "--use-usr" ]; then
    USR="--use-usr"
  fi
  if  [ "$var" = "--production" ]; then
    HTML_DIR="/var/www/html/clang-ctu"
    WEB_ROOT="/clang-ctu"
    CODECHECKER_PORT="8080"
    MODE="prod"
  fi
  if  [ "$var" = "--name" ]; then
    j=$((i+1))
    echo "index is $j"
    args=("$@")
    NAME="--name "${args[j]}
    echo "Run Name: $NAME"
  fi
  i=$((i+1))
done
STATFILE="all_statistics_"$MODE.txt


for PROJECT in $(ls -d */ | cut -d"/" -f1); do
  if [ "$MEMPROF" = true ]; then
    ../analyze_project.sh $PROJECT $REPARSE --memprof $USR $NAME
    cp ./$PROJECT/build/*heap_usage.txt $HTML_DIR
  else
    /bin/bash -x ../analyze_project.sh $PROJECT $REPARSE $USR $NAME
  fi
  cp -rf $PROJECT/build/*gcov* $HTML_DIR
done

cd ..
cat projects/statistics.txt >> $STATFILE
cat projects/detailed_stats.txt >> all_detailed_$STATFILE

#no need to restart codechecker
#kill $(ps ux | grep "CodeChecker/bin/CodeChecker server -w ccdb/" | grep -v "grep" | awk '{print $2}')
#sleep 3
#clang_build/CodeChecker/bin/CodeChecker server -w ccdb/ --not-host-only -v 8080 &
#sleep 2
./generate_html.sh $HTML_DIR $WEB_ROOT $CODECHECKER_PORT $STATFILE

#python2.7 generateStatTable.py projects/detailed_stats.txt
