#!/usr/bin/python

import subprocess, time, signal, random

for _ in range(50):
	p = subprocess.Popen(["./build/FlowMown-DPDK", "-l", "0,2,4,6,8", "-n", "4", "--", "-P", "-p", "8", '--rx="(3,0,2,0)(3,1,4,1)"', '--tx="(6,0)(8,1)"', "--write-file"])
	time.sleep(370)
	p.send_signal(signal.SIGINT)
	time.sleep(6)
