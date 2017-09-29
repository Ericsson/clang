#!/bin/sh

TEMP=`getopt -o t: --long threshold -n gen_yaml_for_return_value_checks.sh -- "$@"`

if [ $? != 0 ] ; then exit 1 ; fi

eval set -- "$TEMP"

THRESHOLD=85

while true; do
  case "$1" in
    -t | --threshold ) THRESHOLD="$2"; shift 2 ;;
    * ) break ;;
  esac
done

fgrep "Return Value Check" |\
cut -d\: -f6- |\
sort |\
uniq |\
cut -d, -f2- |\
awk -F, '
BEGIN {
  printf "#\n# UncheckedReturn metadata format 1.0\n\n"
}

{
  total[$1]++;
  unchecked[$1]+=$2
}

END {
  for (i in total) {
    percentage = 100*unchecked[i]/total[i]
    if (percentage < 100 - "'"$THRESHOLD"'") {
       printf "- %s\n", i
    }
  } 
}
'
