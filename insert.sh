#!/bin/bash

# Roberto Masocco <robmasocco@gmail.com>
# May 2, 2021
# AOS-TAG project modules loading script.

# Displays help information and exits.
usage() {
    echo "Usage: insert.sh MAX_TAGS MAX_MSG_SZ" 1>&2;
    echo "  MAX_TAGS: Max number of tags (will be passed to insmod)." 1>&2;
    echo "  MAX_MSG_SZ: Max allowed size of a message, in bytes (will be passed to insmod)." 1>&2;
    echo "Values will now be set to defaults." 1>&2;
}

# Verify that the PWD is the project's root directory.
CURR_DIR=${PWD##*/}
REQ_CURR_DIR="AOS-TAG"
if [[ $CURR_DIR != $REQ_CURR_DIR ]]; then
	echo "ERROR: Wrong path, this script must be executed inside $REQ_CURR_DIR." 1>&2
	exit 1
fi

# Parse input arguments.
if [[ $# -ne 2 ]]; then
    usage
    MAX_TAGS=256
    MAX_MSG_SZ=4096
else
    MAX_TAGS=$1
    MAX_MSG_SZ=$2
fi
echo "MAX_TAGS: $MAX_TAGS"
echo "MAX_MSG_SZ: $MAX_MSG_SZ"

# Compile modules.
echo "Compiling modules..."
cd aos-tag/
make
if [[ $? -ne 0 ]]; then
    echo "ERROR: Failed to compile and build project modules." 1>&2
    cd ..
    exit 1
fi

# Insert modules.
echo "Inserting SCTH module..."
sudo insmod scth/scth.ko
if [[ $? -ne  0 ]]; then
    echo "ERROR: Failed to insert SCTH module." 1>&2
    exit 1
fi

echo "Inserting AOS-TAG module..."
sudo insmod aos_tag.ko max_tags=$MAX_TAGS max_msg_sz=$MAX_MSG_SZ
if [[ $? -ne 0 ]]; then
    echo "ERROR: Failed to insert AOS-TAG module." 1>&2
    sudo rmmod scth
    exit 1
fi

echo "Modules inserted!"
cd ../

# Show AOS-TAG module parameters.
echo "Max number of tag instances allowed: $(cat /sys/module/aos_tag/parameters/max_tags)"
echo "Max message size: $(cat /sys/module/aos_tag/parameters/max_msg_sz)"
echo "tag_get system call installed at: $(cat /sys/module/aos_tag/parameters/tag_get_nr)"
echo "tag_receive system call installed at: $(cat /sys/module/aos_tag/parameters/tag_receive_nr)"
echo "tag_send system call installed at: $(cat /sys/module/aos_tag/parameters/tag_send_nr)"
echo "tag_ctl system call installed at: $(cat /sys/module/aos_tag/parameters/tag_ctl_nr)"
echo "Device driver registered with major number: $(cat /sys/module/aos_tag/parameters/tag_drv_major)"

# Generate userspace header.
HEADER_NAME=aos-tag.h
HEADER_PATH=aos-tag/include/$HEADER_NAME
echo "Generating userspace header..."
sed "s/#define __NR_tag_get 134/#define __NR_tag_get $(cat /sys/module/aos_tag/parameters/tag_get_nr)/" $HEADER_PATH > $HEADER_NAME
if [[ $? -ne 0 ]]; then
    echo "ERROR: Failed to generate userspace header." 1>&2
    exit 1
fi
sed -i -e "s/#define __NR_tag_receive 174/#define __NR_tag_receive $(cat /sys/module/aos_tag/parameters/tag_receive_nr)/" $HEADER_NAME
if [[ $? -ne 0 ]]; then
    echo "ERROR: Failed to generate userspace header." 1>&2
    exit 1
fi
sed -i -e "s/#define __NR_tag_send 177/#define __NR_tag_send $(cat /sys/module/aos_tag/parameters/tag_send_nr)/" $HEADER_NAME
if [[ $? -ne 0 ]]; then
    echo "ERROR: Failed to generate userspace header." 1>&2
    exit 1
fi
sed -i -e "s/#define __NR_tag_ctl 178/#define __NR_tag_ctl $(cat /sys/module/aos_tag/parameters/tag_ctl_nr)/" $HEADER_NAME
if [[ $? -ne 0 ]]; then
    echo "ERROR: Failed to generate userspace header." 1>&2
    exit 1
fi
echo "Userspace header file name: $HEADER_NAME"
echo "All done!"
