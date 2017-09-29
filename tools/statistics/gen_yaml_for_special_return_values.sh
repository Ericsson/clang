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

fgrep "Special Return Value" |\
cut -d\: -f6- |\
sort |\
uniq |\
cut -d, -f2- |\
awk -F, '
BEGIN {
  printf "#\n# SpecialReturn metadata format 1.0\n\n"
}

{
  total[$1]++;
  forNegative[$1]+=$2;
  forNull[$1]+=$3
}

END {
  for (i in total) {
    negPercent = 100*forNegative[i]/total[i]
    nullPercent = 100*forNull[i]/total[i]
    if (negPercent >= "'"$THRESHOLD"'") {
       printf "{name: %s, relation: LT, value: 0}\n", i
    }
    if (nullPercent >= "'"$THRESHOLD"'") {
       printf "{name: %s, relation: EQ, value: 0}\n", i
    }
  } 
}
'
