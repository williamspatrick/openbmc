#!/bin/bash
#
# Copyright 2014-present Facebook. All Rights Reserved.
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program in a file named COPYING; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA
#

input=$1


print_usage() {

    echo "  slot to host name lookup"
    echo ""
    echo "  Usage:      slot-util [ slot1, slot2, slot3, slot4, all ] : shows host for a particular/all slot(s)"
    echo "              slot-util [ 1, 2, 3, 4 ]                      : shows host for a particular slot"
    echo "              slot-util [ hostname ]                        : shows slot number for a host"
}

if [ $# -eq 0 ]; then
    print_usage
    exit 1
fi


# get BMC mac address and conver it to base 10 integer
bmc_mac_dec="$( printf "%d\\n" "0x$(sed s/://g "/sys/class/net/eth0/address")")"


# get neighboring devices in {hostname, MAC} format, shows only entries that can resolve to host name
host_table=$(ip -r neigh show dev eth0 | grep -iE 'com|edu|gov|org' | sort -k 3)

# calculate expected MAC address for each slot, convert it back to hex str
slot1_mac=$(printf "%012x\\n" $((bmc_mac_dec-1)) | sed -e 's/[0-9A-Fa-f]\{2\}/&:/g' -e 's/:$//')
slot2_mac=$(printf "%012x\\n" $((bmc_mac_dec+1)) | sed -e 's/[0-9A-Fa-f]\{2\}/&:/g' -e 's/:$//')
slot3_mac=$(printf "%012x\\n" $((bmc_mac_dec+3)) | sed -e 's/[0-9A-Fa-f]\{2\}/&:/g' -e 's/:$//')
slot4_mac=$(printf "%012x\\n" $((bmc_mac_dec+5)) | sed -e 's/[0-9A-Fa-f]\{2\}/&:/g' -e 's/:$//')

# match host name to each slot based on MAC address
# shellcheck disable=SC2034
host1="$(echo "$host_table" | grep -i "$slot1_mac" | cut  -d " " -f1)"
# shellcheck disable=SC2034
host2="$(echo "$host_table" | grep -i "$slot2_mac" | cut  -d " " -f1)"
# shellcheck disable=SC2034
host3="$(echo "$host_table" | grep -i "$slot3_mac" | cut  -d " " -f1)"
# shellcheck disable=SC2034
host4="$(echo "$host_table" | grep -i "$slot4_mac" | cut  -d " " -f1)"


# check if user asked for a specific slot, either in "slotX", or just "X"
if [[ $((input)) == "$1" ]]; then
    slot_num=$input
elif [[ $1 == "slot"* ]]; then
    slot_num=$(echo "$1" | tr -dc '0-9')
fi


# check if user specified "all" option
if [ -z "$slot_num" ]; then
    if [[ $1 == "all" ]]; then
        for i in 1 2 3 4
        do
           eval echo slot$i: \$host$i
        done
    else
        # check if user entered a valid host name
        match=$(echo "$host_table" | grep -i "$1")
        # if no match
        if [ -z "$match" ]; then
            echo host name "$1" not found
        else
            for i in 1 2 3 4
            do
               hostname=$( (eval echo \$host$i) | grep -i "$1")
               if [ -z "$hostname" ]; then
                   :
               else
                   eval echo slot$i: \$host$i
               fi
            done
        fi
    fi
else
    # user specified a specific slot
    eval echo slot"$slot_num": \$host"$slot_num"
fi
