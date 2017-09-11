#!/bin/bash
if [ -z $1 ] || [ $1 = "-h" ] || [ $1 = "--help" ] || [ ! -d $1 ]; then
  echo "Usage: "$0" <project-name> [--memprof] [--production] [--reparse]"
  echo "--memprof creates valgrind massif memory profile"
  echo "--production publish results in the public production database. The default is non-production mode."
  echo "--reparse Do not generate ast dumps, analyze on the fly"
  echo "--use-usr Use USR identifiers instead of mangled names"
  echo "--name Run name extension"
  exit 0
fi

MEMPROF=""
CODECHECKER_PORT=15002
REPARSE=""
USR=""
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
    # REPARSE="--xtu-reparse"
    REPARSE="--ctu-on-the-fly"
  fi
  if  [ "$var" = "--use-usr" ]; then
    USR="--use-usr"
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
rm -rf .xtu/
rm -rf .xtu-out-init/
rm -rf .xtu-out-noxtu/
cp -rf .xtu-out-xtu ./"$PROJNAME"_xtu-out-xtu
rm -rf .xtu-out-xtu/
#INITIAL XTU RUN
#/usr/bin/time -f "%e" ../../../clang_build/clang/tools/xtu-build-new/xtu-build.py $USR $REPARSE -b buildlog.json --clang-path /mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/ -v -j 16 2>"$PROJNAME"_buildtime.out | tee "$PROJNAME"_build.out
#../../../clang_build/clang/tools/xtu-build-new/xtu-analyze.py $USR $REPARSE -b buildlog.json -o .xtu-out-init -j 16 -v --clang-path /mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/ --analyze-cc-path /mnt/storage/xtu-service/clang_build/clang/tools/scan-build-py/bin/ --log-passed-build passed_buildlog.json | tee "$PROJNAME"_init.out
#NOXTU-RUN
#../../../clang_build/clang/tools/xtu-build-new/xtu-analyze.py $MEMPROF --record-coverage -b passed_buildlog.json -o .xtu-out-noxtu -j 16 -v --clang-path /mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/ --analyze-cc-path /mnt/storage/xtu-service/clang_build/clang/tools/scan-build-py/bin/ --no-xtu | tee "$PROJNAME"_noXTU.out
PATH=/mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/:$PATH $CC analyze -o .xtu-out-noxtu --analyzers clangsa -j 16 buildlog.json | tee "$PROJNAME"_noXTU.out
#mkdir "$PROJNAME"_gcovNoXtu
#gcovr -k -g .xtu-out-noxtu/gcov --html --html-details -r . -o "$PROJNAME"_gcovNoXtu/coverage.html
#XTU RUN (on files that dont crash)
#../../../clang_build/clang/tools/xtu-build-new/xtu-analyze.py $USR $MEMPROF $REPARSE --record-coverage -b passed_buildlog.json -o .xtu-out-xtu -j 16 -v --clang-path /mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/ --analyze-cc-path /mnt/storage/xtu-service/clang_build/clang/tools/scan-build-py/bin/ | tee "$PROJNAME"_XTU.out
PATH=/mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/:$PATH $CC analyze -o .xtu-out-xtu --analyzers clangsa --ctu-collect $REPARSE -j 16 buildlog.json | tee "$PROJNAME"_XTU_build.out
PATH=/mnt/storage/xtu-service/clang_build/buildrelwdeb/bin/:$PATH $CC analyze -o .xtu-out-xtu --analyzers clangsa --ctu-analyze $REPARSE -j 16 buildlog.json | tee "$PROJNAME"_XTU_analyze.out

#mkdir "$PROJNAME"_gcovXtu
#gcovr -k -g .xtu-out-xtu/gcov --html --html-details -r . -o "$PROJNAME"_gcovXtu/coverage.html
$CC store .xtu-out-noxtu -n "$PROJNAME"_noXTU -p $CODECHECKER_PORT -j 1
$CC store .xtu-out-xtu -n "$PROJNAME"_XTU -p $CODECHECKER_PORT -j 1
#mkdir "$PROJNAME"_gcovDiff
#python ../../../clang_build/clang/utils/analyzer/MergeCoverage.py -b .xtu-out-xtu/gcov -i .xtu-out-noxtu/gcov -o "$PROJNAME"_gcovDiff
#gcovr -k -g "$PROJNAME"_gcovDiff --html --html-details -r . -o "$PROJNAME"_gcovDiff/coverage.html
#FILES_XTU=$(cat "$PROJNAME"_init.out | grep "\-\-\- Total files analyzed:" | cut -d" " -f5)
#PASSES_XTU=$(cat "$PROJNAME"_init.out | grep "\-\-\-\-\- Files passed:" | cut -d" " -f4)
#FAILS_XTU=$(cat "$PROJNAME"_init.out | grep "\-\-\-\-\- Files failed:" | cut -d" " -f4)
#TIME_XTU=$(cat "$PROJNAME"_XTU.out | grep "\-\-\- Total running time:" | cut -d" " -f5 | cut -d"s" -f1)
#TIME_NOXTU=$(cat "$PROJNAME"_noXTU.out | grep "\-\-\- Total running time:" | cut -d" " -f5 | cut -d"s" -f1)
#TIME_BUILD=$(tail -n 1 "$PROJNAME"_buildtime.out)
FILES_XTU=$(cat "$PROJNAME"_XTU_analyze.out | grep "Total compilation commands:" | cut -d" " -f6)
PASSES_XTU=$(cat "$PROJNAME"_XTU_analyze.out | grep -A 1 "Successfully analyzed" | tail -n 1 | cut -d" " -f6)
FAILS_XTU=$(cat "$PROJNAME"_XTU_analyze.out | grep -A 1 "Failed to analyze" | tail -n 1 | cut -d" " -f6)
TIME_XTU=$(cat "$PROJNAME"_XTU_analyze.out | grep "Analysis length:" | cut -d" " -f5)
TIME_NOXTU=$(cat "$PROJNAME"_noXTU.out | grep "Analysis length:" | cut -d" " -f5)
TIME_BUILD=$(cat "$PROJNAME"_XTU_build.out | grep "Analysis length:" | cut -d" " -f5)
if [ -z "$PASSES_XTU" ]; then
  PASSES_XTU=0
fi
if [ -z "$FAILS_XTU" ]; then
  FAILS_XTU=0
fi
MEM_NOXTU="0"
MEM_XTU="0"
if [ "$MEMPROF" = "--record-memory-profile" ]; then
  MEM_NOXTU=$(python2.7 ../../../massif_stats.py -M -m -d ./.xtu-out-noxtu/memprof)
  MEM_XTU=$(python2.7 ../../../massif_stats.py -M -m -d ./.xtu-out-xtu/memprof)
  python2.7 ../../../massif_stats.py -M -m -p -d ./.xtu-out-xtu/memprof > ./"$PROJNAME"_XTU_heap_usage.txt
  python2.7 ../../../massif_stats.py -M -m -p -d ./.xtu-out-noxtu/memprof > ./"$PROJNAME"_NoXTU_heap_usage.txt
fi

#echo "Project Name: $PROJNAME"  >> ../../detailed_stats.txt
#echo "noXTU" >> ../../detailed_stats.txt
#python2.7 ../../../summarizeClangSAStats.py "$PROJNAME"_noXTU.out >> ../../detailed_stats.txt
#echo "XTU" >> ../../detailed_stats.txt
#python2.7 ../../../summarizeClangSAStats.py "$PROJNAME"_XTU.out >> ../../detailed_stats.txt

cd ../..
echo "Project result format: project-id total-files passed-files-XTU failed-files-XTU time-of-XTU time-of-noXTU time-of-xtu-build heap-usage-noxtu heap-usage-xtu"
echo "--- Project results:" "$PROJECT"_"$TIMESTAMP" $FILES_XTU $PASSES_XTU $FAILS_XTU $TIME_XTU $TIME_NOXTU $TIME_BUILD $MEM_NOXTU $MEM_XTU
echo "$PROJNAME $FILES_XTU $PASSES_XTU $FAILS_XTU $TIME_XTU $TIME_NOXTU $TIME_BUILD $MEM_NOXTU $MEM_XTU" >> statistics.txt

