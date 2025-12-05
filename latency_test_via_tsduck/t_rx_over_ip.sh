# This is useful to measure e2e latency purly in SDR (tx and rx)
# GRC outputs at port 2000 udp
tsp --realtime=on -I ip 2000 -P mpe --pid 32 --udp-forward --redirect 127.0.0.1:9006 -O drop
