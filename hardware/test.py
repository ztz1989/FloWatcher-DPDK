#!/usr/bin/python

import subprocess, time, signal

for _ in range(50):
	p = subprocess.Popen(["./build/FlowMown-DPDK", "-c", "112"])
	time.sleep(370)
	p.send_signal(signal.SIGINT)
	time.sleep(6)
