#!/bin/bash
#filename with space is passed to gcc
PATH=../../scan-build-py/bin:../../clang-func-mapping/:../:$PATH
echo "USING clang "`which clang`
rm ./build.json
intercept-build -v --cdb build.json /bin/bash -c "gcc -c ./caller.c ;gcc -c ./lib.c;gcc -c './lib 2.c'"
rm -rf ./.ctu
ctu-build.py -j1 -v -b build.json -p .ctu
rm -rf ./.ctu-out
ctu-analyze.py -j1 -o ./.ctu-out -v -p .ctu -b ./build.json
nof_faults=`cat ./.ctu-out/*|grep "<key>type"|wc -l`
echo "Number of faults found:$nof_faults"
if [ $nof_faults = "4" ]
then
    echo "PASSED"
else
    echo "FAILED"
fi