#!/bin/bash

# Batch Convert .ichat Files
# Below are some sample invocations of "Convert ichat Files". Run the program without arguments to get the help page.

IFS="
"

if [ ! -d "$1" ]; then
   echo "You need to supply a directory to me!"
   exit
fi

for THE_FILE in `find "$1" | grep -E "\.ichat"`
do
   #"./Build/Convert ichat Files" -mode convert -input "$THE_FILE" -format RTF --trim-email-ids --overwrite
   #"./Build/Convert ichat Files" -mode convert -input "$THE_FILE" -format RTF --overwrite
   #"./Build/Convert ichat Files" -mode convert -input "$THE_FILE" -format RTF --real-names
   "./Build/Convert ichat Files" -mode convert -input "$THE_FILE" -format TXT --real-names --overwrite
done