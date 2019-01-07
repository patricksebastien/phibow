#!/bin/sh
cd /home/psc/11h11/projects/phibow
xset s 0 0
xset s off
sudo service apache2 stop
pd-extended -rt -nosleep -jack -channels 2 -audiobuf 5 -alsamidi -mididev 1 -open phibowAudio.pd &
sleep 2
pd-extended -nrt -noaudio -open phibowVideo.pd &
sudo nohup node node/app.js &
#xdg-open www.pdpatchrepo.info/cstream/tos/16 &
while [ true ]; do
	sleep 3
	echo startingstream
	gst-launch-0.10 -e jackaudiosrc ! queue2 ! audioconvert ! queue2 ! audiorate ! queue2 ! ffenc_aac ! queue2 ! flashmux. ximagesrc use-damage=0 startx=0 starty=0 endx=767 endy=431 ! queue2 ! videoscale ! video/x-raw-rgb,width=768,height=432,framerate=30/1 ! ffmpegcolorspace ! videorate ! queue2 ! x264enc threads=0 bitrate=1200 subme=10 tune=zerolatency ! flvmux name="flashmux" ! rtmpsink location="rtmp://stream.puredata.info/rtmp/stream?st=mc1UNWshWk3c0zGN_80z_w live=1" sync=false
done
xdg-open www.pdpatchrepo.info/cstream/tfs
sudo killall node
sudo killall pd-extended
