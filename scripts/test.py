#!/usr/bin/python
# The script to test MoonGen RX functionality

import subprocess, time, signal, random

for _ in range(50):
	p = subprocess.Popen(["/home/tzhang/MoonGen/build/MoonGen", "flow-level.lua", "3"])
	time.sleep(370)
	p.send_signal(signal.SIGINT)
	time.sleep(6)
