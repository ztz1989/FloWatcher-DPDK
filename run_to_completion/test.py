#!/usr/bin/python
# The script to test the RX functionality

import subprocess, time, signal, random

for _ in range(50):
	p = subprocess.Popen(["./build/FlowMon-DPDK", "-c", "111"])
	time.sleep(370)
	p.send_signal(signal.SIGINT)
	time.sleep(6)
