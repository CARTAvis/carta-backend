#!/bin/bash

# Execute this bash script in the current folder, and it will normalize the guard names from all header files

function CheckGuardNameStyle () {
  filename=$1

  # Get the guard name
  guard_name="CARTA_"

  # Split the file path name by '/' as an array
  sub_names=(${filename//\// })

  # Concatenate the sub-names of the file path name
  for i in "${sub_names[@]}"
  do
    guard_name+=$i
    guard_name+="_"
  done

  # Capitalize all letters
  guard_name=$(echo "${guard_name}" | tr '[:lower:]' '[:upper:]')

  # Replace '.' with '_'
  guard_name=$(echo "${guard_name}" | sed "s/\./\_/g")

  # Get the line number for the first occurrence of '#ifndef ...'
  line_num=$(grep -m1 -n -- '^#ifndef' "${filename}")
  line_num="${line_num%%:*}"

  if [[ $line_num -gt 0 ]]; then
    # Replace with the guard name in the line number for the first occurrence of '#ifndef ...'
    if [[ "${OSTYPE}" == "darwin"* ]]; then
      sed -i '' "${line_num}s/.*/#ifndef ${guard_name}/" "${filename}"
    else
      sed -i "${line_num}s/.*/#ifndef ${guard_name}/" "${filename}"
    fi

    # Get the line number after the first occurrence of '#ifndef ...'
    line_num=$((line_num + 1))

    # Replace with the guard name in the line number after the first occurrence of '#ifndef ...'
    if [[ "${OSTYPE}" == "darwin"* ]]; then
      sed -i '' "${line_num}s/.*/#define ${guard_name}/" "${filename}"
    else
      sed -i "${line_num}s/.*/#define ${guard_name}/" "${filename}"
    fi
  fi

  # Replace the last occurrence of matched '#endif ...'
  if [[ "${OSTYPE}" == "darwin"* ]]; then
    sed -i '' "$ s/^#endif .*$/#endif \/\/ ${guard_name}/" "${filename}"
  else
    sed -i "$ s/^#endif .*$/#endif \/\/ ${guard_name}/" "${filename}"
  fi
}

function CheckFolder() {
  # Get file names
  folder=$1
  suffix=$2
  filenames=($(find "${folder}" -name "${suffix}"))

  # Replace the guard names for each of the files
  for j in "${filenames[@]}"
  do
    filename=$j
    CheckGuardNameStyle "${filename}"
  done
}

# Go to the base folder
cd ..

# Do replacements
CheckFolder "src" "*.h"
CheckFolder "src" "*.tcc"

CheckFolder "test" "*.h"
CheckFolder "test" "*.tcc"
