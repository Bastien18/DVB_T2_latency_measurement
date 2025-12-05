# Listen to udp port 9005 and inject data. Make a mpegts with 4305532 of bitrate
tsp -I null -P regulate --bitrate 4305532 -P mpeinject --max-queue 1 --pid 32 9005 -O file tspipe