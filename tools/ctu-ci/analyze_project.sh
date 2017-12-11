#!/bin/bash
if [ -z $1 ] || [ $1 = "-h" ] || [ $1 = "--help" ] || [ ! -d $1 ]; then
  echo "Usage: "$0" <project-name> [--memprof] [--production] [--reparse]"
  echo "--memprof creates valgrind massif memory profile"
  echo "--production publish results in the public production database. The default is non-production mode."
  echo "--reparse Do not generate ast dumps, analyze on the fly"
  echo "--use-usr Use USR identifiers instead of mangled names"
  echo "--name Run name extension"
  echo "--strict-mode Give an error on all import failures"
  exit 0
fi

MEMPROF=""
CODECHECKER_PORT=15002
REPARSE=""
USR=""
STRICT_MODE=""
NAME=""
i=0
j=0
for var in "$@"
do
  if  [ "$var" = "--memprof" ]; then
    MEMPROF="--record-memory-profile"
  fi
  if  [ "$var" = "--production" ]; then
    CODECHECKER_PORT=8080
  fi
  if  [ "$var" = "--reparse" ]; then
    # REPARSE="--ctu-reparse"
    REPARSE="--ctu-on-the-fly"
  fi
  if  [ "$var" = "--use-usr" ]; then
    USR="--use-usr"
  fi
  if  [ "$var" = "--strict-mode" ]; then
    STRICT_MODE="--saargs strict_mode_saargs.txt"
  fi
  if  [ "$var" = "--name" ]; then
    j=$((i+1))
    echo "index is $j"
    args=("$@")
    NAME="_"${args[j]}
    echo "Run Name: $NAME"
  fi
  i=$((i+1))
done

.  /mnt/storage/xtu-service/clang_build/codechecker/venv/bin/activate

CC="/mnt/storage/xtu-service/clang_build/codechecker/build/CodeChecker/bin/CodeChecker"
PROJECT="$1"
TIMESTAMP=$(date +"%F_%T")
PROJNAME="$PROJECT"_"$TIMESTAMP$NAME"

echo "Running project with ID ""$PROJECT"_"$TIMESTAMP"
cd $PROJECT
cd build
rm -rf .ctu/
rm -rf .ctu-out-init/
rm -rf .ctu-out-noctu/
cp -rf .ctu-out-ctu ./"$PROJNAME"_ctu-out-ctu
rm -rf .ctu-out-ctu/
#INITIAL CTU RUN
#/usr/bin/time -f "%e" ../../../clang_build/clang/tools/ctu-build-new/ctu-build.py $USR $REPARSE -b buildlog.json --clang-path /mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/ -v -j 16 2>"$PROJNAME"_buildtime.out | tee "$PROJNAME"_build.out
#../../../clang_build/clang/tools/ctu-build-new/ctu-analyze.py $USR $REPARSE -b buildlog.json -o .ctu-out-init -j 16 -v --clang-path /mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/ --analyze-cc-path /mnt/storage/xtu-service/clang_build/clang/tools/scan-build-py/bin/ --log-passed-build passed_buildlog.json | tee "$PROJNAME"_init.out
#NOCTU-RUN
#../../../clang_build/clang/tools/ctu-build-new/ctu-analyze.py $MEMPROF --record-coverage -b passed_buildlog.json -o .ctu-out-noctu -j 16 -v --clang-path /mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/ --analyze-cc-path /mnt/storage/xtu-service/clang_build/clang/tools/scan-build-py/bin/ --no-ctu | tee "$PROJNAME"_noCTU.out
PATH=/mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/:$PATH $CC analyze -o .ctu-out-noctu --analyzers clangsa -j 16 buildlog.json | tee "$PROJNAME"_noCTU.out
#mkdir "$PROJNAME"_gcovNoCtu
#gcovr -k -g .ctu-out-noctu/gcov --html --html-details -r . -o "$PROJNAME"_gcovNoCtu/coverage.html
#CTU RUN (on files that dont crash)
#../../../clang_build/clang/tools/ctu-build-new/ctu-analyze.py $USR $MEMPROF $REPARSE --record-coverage -b passed_buildlog.json -o .ctu-out-ctu -j 16 -v --clang-path /mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/ --analyze-cc-path /mnt/storage/xtu-service/clang_build/clang/tools/scan-build-py/bin/ | tee "$PROJNAME"_CTU.out
PATH=/mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/:$PATH $CC analyze -o .ctu-out-ctu --analyzers clangsa --ctu-collect $REPARSE $STRICT_MODE -j 16 buildlog.json | tee "$PROJNAME"_CTU_build.out
PATH=/mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/:$PATH $CC analyze -o .ctu-out-ctu --analyzers clangsa --ctu-analyze $REPARSE $STRICT_MODE -j 16 buildlog.json | tee "$PROJNAME"_CTU_analyze.out

#mkdir "$PROJNAME"_gcovCtu
#gcovr -k -g .ctu-out-ctu/gcov --html --html-details -r . -o "$PROJNAME"_gcovCtu/coverage.html
$CC store .ctu-out-noctu -n "$PROJNAME"_noCTU -p $CODECHECKER_PORT -j 1
$CC store .ctu-out-ctu -n "$PROJNAME"_CTU -p $CODECHECKER_PORT -j 1
#mkdir "$PROJNAME"_gcovDiff
#python ../../../clang_build/clang/utils/analyzer/MergeCoverage.py -b .ctu-out-ctu/gcov -i .ctu-out-noctu/gcov -o "$PROJNAME"_gcovDiff
#gcovr -k -g "$PROJNAME"_gcovDiff --html --html-details -r . -o "$PROJNAME"_gcovDiff/coverage.html
#FILES_CTU=$(cat "$PROJNAME"_init.out | grep "\-\-\- Total files analyzed:" | cut -d" " -f5)
#PASSES_CTU=$(cat "$PROJNAME"_init.out | grep "\-\-\-\-\- Files passed:" | cut -d" " -f4)
#FAILS_CTU=$(cat "$PROJNAME"_init.out | grep "\-\-\-\-\- Files failed:" | cut -d" " -f4)
#TIME_CTU=$(cat "$PROJNAME"_CTU.out | grep "\-\-\- Total running time:" | cut -d" " -f5 | cut -d"s" -f1)
#TIME_NOCTU=$(cat "$PROJNAME"_noCTU.out | grep "\-\-\- Total running time:" | cut -d" " -f5 | cut -d"s" -f1)
#TIME_BUILD=$(tail -n 1 "$PROJNAME"_buildtime.out)
FILES_CTU=$(cat "$PROJNAME"_CTU_analyze.out | grep "Total compilation commands:" | cut -d" " -f6)
PASSES_CTU=$(cat "$PROJNAME"_CTU_analyze.out | grep -A 1 "Successfully analyzed" | tail -n 1 | cut -d" " -f6)
FAILS_CTU=$(cat "$PROJNAME"_CTU_analyze.out | grep -A 1 "Failed to analyze" | tail -n 1 | cut -d" " -f6)
TIME_CTU=$(cat "$PROJNAME"_CTU_analyze.out | grep "Analysis length:" | cut -d" " -f5)
TIME_NOCTU=$(cat "$PROJNAME"_noCTU.out | grep "Analysis length:" | cut -d" " -f5)
TIME_BUILD=$(cat "$PROJNAME"_CTU_build.out | grep "Analysis length:" | cut -d" " -f5)
if [ -z "$PASSES_CTU" ]; then
  PASSES_CTU=0
fi
if [ -z "$FAILS_CTU" ]; then
  FAILS_CTU=0
fi
MEM_NOCTU="0"
MEM_CTU="0"
if [ "$MEMPROF" = "--record-memory-profile" ]; then
  MEM_NOCTU=$(python2.7 ../../../massif_stats.py -M -m -d ./.ctu-out-noctu/memprof)
  MEM_CTU=$(python2.7 ../../../massif_stats.py -M -m -d ./.ctu-out-ctu/memprof)
  python2.7 ../../../massif_stats.py -M -m -p -d ./.ctu-out-ctu/memprof > ./"$PROJNAME"_CTU_heap_usage.txt
  python2.7 ../../../massif_stats.py -M -m -p -d ./.ctu-out-noctu/memprof > ./"$PROJNAME"_NoCTU_heap_usage.txt
fi

#echo "Project Name: $PROJNAME"  >> ../../detailed_stats.txt
#echo "noCTU" >> ../../detailed_stats.txt
#python2.7 ../../../summarizeClangSAStats.py "$PROJNAME"_noCTU.out >> ../../detailed_stats.txt
#echo "CTU" >> ../../detailed_stats.txt
#python2.7 ../../../summarizeClangSAStats.py "$PROJNAME"_CTU.out >> ../../detailed_stats.txt

cd ../..
echo "Project result format: project-id total-files passed-files-CTU failed-files-CTU time-of-CTU time-of-noCTU time-of-ctu-build heap-usage-noctu heap-usage-ctu"
echo "--- Project results:" "$PROJECT"_"$TIMESTAMP" $FILES_CTU $PASSES_CTU $FAILS_CTU $TIME_CTU $TIME_NOCTU $TIME_BUILD $MEM_NOCTU $MEM_CTU
echo "$PROJNAME $FILES_CTU $PASSES_CTU $FAILS_CTU $TIME_CTU $TIME_NOCTU $TIME_BUILD $MEM_NOCTU $MEM_CTU" >> statistics.txt

