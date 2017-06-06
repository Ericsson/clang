#!/bin/bash
##Two source files passed to gcc
PATH=../../scan-build-py/bin:../../clang-func-mapping/:../:$PATH
echo "USING clang "`which clang`
rm ./build.json
~/work/codechecker/CodeChecker/bin/CodeChecker log -b "gcc -c ./caller.c ./lib.c" -o build.json
rm -rf ./.ctu
ctu-build.py -j1 -v -b build.json -p .ctu
rm -rf ./.ctu-out
ctu-analyze.py -j1 -o ./.ctu-out -v -p .ctu -b ./build.json
nof_faults=`cat ./.ctu-out/*|grep "<key>type"|wc -l`
echo "Number of faults found:$nof_faults"
if [ $nof_faults = "3" ]
then
    echo "PASSED"
else
    echo "FAILED"
fi