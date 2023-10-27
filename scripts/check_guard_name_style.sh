#!/bin/bash

function CheckGuardNameStyle () {
  filename=$1

  # Get the guard name
  guardname="CARTA_"

  names=(${filename//\// })
  for i in "${names[@]}"
  do
    guardname+=$i
    guardname+="_"
  done

  guardname=$(echo $guardname | tr '[:lower:]' '[:upper:]')
  guardname=$(echo $guardname | sed "s/\./\_/g")

  # Get the line number for the first occurence of '#ifndef ...'
  line_num=$(grep -m1 -n -- '^#ifndef' "${filename}")
  line_num="${line_num%%:*}"

  if [[ $line_num -gt 0 ]]; then
    # Replace with the guard name in the line number for the first occurence of '#ifndef ...'
    if [[ "$OSTYPE" == "darwin"* ]]; then
      sed -i '' "${line_num}s/.*/#ifndef $guardname/" $filename
    else
      sed -i "${line_num}s/.*/#ifndef $guardname/" $filename
    fi

    # Get the line number after the first occurence of '#ifndef ...'
    line_num=$((line_num + 1))

    # Replace with the guard name in the line number after the first occurence of '#ifndef ...'
    if [[ "$OSTYPE" == "darwin"* ]]; then
      sed -i '' "${line_num}s/.*/#define $guardname/" $filename
    else
      sed -i "${line_num}s/.*/#define $guardname/" $filename
    fi
  fi

  # Replace the last occurence of matched '#endif ...'
  if [[ "$OSTYPE" == "darwin"* ]]; then
    sed -i '' "$ s/^#endif .*$/#endif \/\/ $guardname/" $filename
  else
    sed -i "$ s/^#endif .*$/#endif \/\/ $guardname/" $filename
  fi
}

function CheckFolder() {
  # Get file names
  folder=$1
  suffix=$2
  filenames=( $(find $folder -name $suffix) )

  # Replace the guard names for each of the files
  for j in "${filenames[@]}"
  do
    filename=$j
    CheckGuardNameStyle $filename
  done
}

# Go to the base folder
cd ..

# Do replacements
CheckFolder "src" "*.h"
CheckFolder "src" "*.tcc"

CheckFolder "test" "*.h"
CheckFolder "test" "*.tcc"

