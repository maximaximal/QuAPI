#!/usr/bin/env bash
while read line
do
	echo "$line" >> out
done < "${1:-/dev/stdin}"
exit 20

