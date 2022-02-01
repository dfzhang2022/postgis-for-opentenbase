#!/usr/bin/env bash

# Exit on first error
set -e

mkfifo check.fifo
tee check.log < check.fifo &
echo "teepid=$teepid" > check.fifo
wait
cat check.log
