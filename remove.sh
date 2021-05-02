#!/bin/bash

# Roberto Masocco <robmasocco@gmail.com>
# May 2, 2021
# AOS-TAG project modules unloading script.

echo "Removing AOS-TAG module..."
sudo rmmod aos_tag
if [[ $? -ne 0 ]]; then
    echo "ERROR: Failed to remove AOS-TAG module." 1>&2
    exit 1
fi

echo "Removing SCTH module..."
sudo rmmod scth
if [[ $? -ne 0 ]]; then
    echo "ERROR: Failed to remove SCTH module." 1>&2
    exit 1
fi
