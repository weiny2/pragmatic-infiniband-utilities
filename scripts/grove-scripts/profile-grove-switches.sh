#!/bin/bash

interval=5

grove_switches=`cat grove_switches`
ion_switches=`cat ion_switches`
spine_switches=`cat spine_switches`
all_switches=`cat all_switches`

basedir=`pwd`
dts=`date +%F_%X`
dir="$basedir/grove-profile-$dts"

mkdir -p $dir

cd ..

echo -n "Scanning Switches: "
./profile-switches.pl -i $interval -L $all_switches -d $dir/all_data -t

echo -n "Sorting Grove Switches: "
./profile-switches.pl -L $grove_switches -r $dir/all_data/rawdata -d $dir/grove_data -t
echo -n "Sorting ION Switches: "
./profile-switches.pl -L $ion_switches -r $dir/all_data/rawdata -d $dir/ion_data -t
echo -n "Sorting Spine Switches: "
./profile-switches.pl -L $spine_switches -r $dir/all_data/rawdata -d $dir/spine_data -t

