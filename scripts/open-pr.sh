#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 5 || $# -gt 6 ]]; then
    echo "usage: $0 <title> <body-file> <base> <head> <milestone> [project]" >&2
    exit 2
fi

title=$1
body_file=$2
base=$3
head=$4
milestone=$5
project=${6:-}

arguments=(
    --title "$title"
    --body-file "$body_file"
    --base "$base"
    --head "$head"
    --assignee @me
    --milestone "$milestone"
)
if [[ -n "$project" ]]; then
    arguments+=(--project "$project")
fi

gh pr create "${arguments[@]}"
