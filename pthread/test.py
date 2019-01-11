#!/usr/bin/python

import subprocess, time, signal, random

for _ in range(50):
	p = subprocess.Popen(["./build/FlowMown-DPDK", "-l", "0,1,3,21,23", "--", "-P", "-p", "8", '--rx="(3,0,1,0)(3,1,3,1)"', '--tx="(21,0)(23,1)"', "--write-file"])

	time.sleep(370)
	p.send_signal(signal.SIGINT)
	time.sleep(6)
