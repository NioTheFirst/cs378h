#!/usr/bin/env bash

file="$1"

prev=""
line_num=0

while read -r curr; do
    ((line_num++))

    if [[ -n "$prev" && "$curr" -lt "$prev" ]]; then
        echo "❌ Not sorted at line $line_num: $prev > $curr"
        exit 1
    fi

    prev="$curr"
done < "$file"

echo "✅ File is sorted"
