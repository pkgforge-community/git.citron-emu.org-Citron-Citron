#!/bin/bash

# Define old and new strings
old="citron"
new="citron"

# Find and rename directories first
find . -depth -type d -name "*${old}*" | while read -r dir; do
    newdir=$(echo "$dir" | sed "s/${old}/${new}/g")
    mv "$dir" "$newdir"
done

# Find and rename files
find . -depth -type f -name "*${old}*" | while read -r file; do
    newfile=$(echo "$file" | sed "s/${old}/${new}/g")
    mv "$file" "$newfile"
done
