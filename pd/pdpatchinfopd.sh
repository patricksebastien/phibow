#!/bin/sh
cd /home/psc/11h11/projects/phibow
xset s 0 0
xset s off
sudo service apache2 stop
pd-extended -rt -nosleep -jack -channels 2 -audiobuf 5 -alsamidi -mididev 1 -open phibowAudio.pd &
sleep 2
pd-extended -nrt -noaudio -open phibowVideo.pd &
sudo nohup node node/app.js &
