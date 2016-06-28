#!/bin/bash

DOT_FILE=$1
COLOR_FILE="./colors.map"
OUT_FILE="./out.dot"
NEW_DOT_FILE="./new.dot"

if [ $# -lt 1 ]
then
  echo "Usage: $0 <dot file>"
  exit 1
fi

if [ -f "${NEW_DOT_FILE}" ] || [ -f "${OUT_FILE}" ]
then
  rm -f ${NEW_DOT_FILE} >/dev/null 2>&1
  rm -f ${OUT_FILE} > /dev/null 2>&1
fi

printf "%-30s\t[%-10s]\n" "Adding color" "Started"

grep "\[.*color=.*\]" ${DOT_FILE} >/dev/null
if [ $? -ne 0 ]
then
	while read -r line
	do
		echo $line | grep "\[.*mx_[a-z]*.*\]" >/dev/null
		if [ $? -eq 0 ]
		then
			newline=`echo $line | sed "s/];/,style=filled,color=COLOR];/g"`
			echo $newline >> ${NEW_DOT_FILE}
		else
			echo $line >> ${NEW_DOT_FILE}
		fi
	done < ${DOT_FILE}
	cp ${DOT_FILE} /tmp/orig.dot.file.tmp
	cp ${NEW_DOT_FILE} ${DOT_FILE}
fi

while read -r line
do
  echo $line | grep "\[.*mx_[a-z]*.*color=COLOR.*\]" > /dev/null
  if [ $? -eq 0 ]
  then
	select=`echo $line | grep -o "mx_[a-z]*"`
	selected=`grep $select ${COLOR_FILE}`
	color=`echo $selected | cut -d= -f 2`
	echo `echo $line | sed "s/COLOR/$color/g"` >> $OUT_FILE
  else
	echo $line >> $OUT_FILE
  fi 
done < "${DOT_FILE}"

cp "${OUT_FILE}" "${DOT_FILE}"

printf "%-30s\t[%-10s]\n" "Adding color" "Done"
