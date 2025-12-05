# This tunes to 634 MHz. Observation is that, the time it takes to tune is not important. Packets are not buffered here.
# Second observation shows there is 160ms buffering somewhere. Meaning that UDP packets just buffered and output at the same time!
tsp --max-input-packets 1 --max-output-packets 1 --realtime=on \
      -I dvb --frequency 634000000 --bandwidth 8 \
      --transmission-mode 2K --delivery-system DVB-T2 --modulation QPSK \
      -P filter --pid 0x0404 -O file - \
| ./pid404_rx
#tsp --timed-log -I dvb --frequency 634000000 --bandwidth 8 --transmission-mode 2K --delivery-system DVB-T2 --modulation QPSK -P trace --pid 0x0404 --format 'PID=%P packet=%i' -O drop