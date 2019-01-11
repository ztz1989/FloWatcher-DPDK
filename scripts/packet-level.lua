--local mg     = require "moongen"
local memory = require "memory"
local device = require "device"
local stats  = require "stats"
local log    = require "log"
local lm     = require "libmoon"
local dpdk   = require "dpdk"
local dpdkc  = require "dpdkc"
local f = io.open("tmp.txt", "a")

function configure(parser)
	parser:argument("rxDev", "The device to receive from"):convert(tonumber)
	parser:option("--rxq", "The RX queue number"):default(2):convert(tonumber)
end

function master(args)
	local rxDev = device.config{port = args.rxDev, rxQueues = args.rxq, rssQueues = args.rxq, rxDescs=4096, numBufs=4500}
	device.waitForLinks()

	stats.startStatsTask{rxDevices={rxDev}}
	for i = 1, args.rxq do
		lm.startTask("dumpSlave", rxDev, rxDev:getRxQueue(i-1), i)
	end
	lm.waitForTasks()
end

function dumpSlave(rxDev, queue, id)
	local bufs = memory.bufArray()
	local pktCtr = stats:newPktRxCounter("Packets counted "..id, "plain")

	--devCtr = stats:newDevRxCounter("Device counter", rxDev, "plain")
	while lm.running() do		
		local rx = queue:tryRecv(bufs, 256)
		for i = 1, rx do
			local buf = bufs[i]
			pktCtr:countPacket(buf)
		end
		bufs:free(rx)		
		pktCtr:update()
	end
	pktCtr:finalize()

	if id==1 then
		local stats = rxDev:getStats()
		print("imissed: " .. tostring(stats.imissed))
		f:write(tostring(stats.ipackets+stats.imissed) .. " " .. tostring(stats.imissed) .. "\n")
		f:close()
	end 
end
