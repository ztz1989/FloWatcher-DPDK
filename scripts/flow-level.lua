--local mg     = require "moongen"
local memory = require "memory"
local device = require "device"
local stats  = require "stats"
local log    = require "log"
local lm     = require "libmoon"

ft = {}
local FLOW_NUM = 65536

for i = 1, FLOW_NUM do
	ft[i-1] = {}
end

function configure(parser)
	parser:argument("rxDev", "The device to receive from"):convert(tonumber)
	parser:option("--rxq", "The RX queue number"):default(2):convert(tonumber)
end

function master(args)
	local rxDev = device.config{port = args.rxDev, rxQueues = args.rxq, rssQueues = args.rxq, numBufs = 8192, dropEnable = false, rxDescs=4096}
	device.waitForLinks()
	stats.startStatsTask{rxDevices={rxDev}}
	for i = 1, args.rxq do
		lm.startTask("dumpSlave", rxDev, rxDev:getRxQueue(i-1))
	end
	lm.waitForTasks()

	-- Query the NIC for Dev statistics
	local f = io.open("tmp.txt", "a")
	local stats = rxDev:getStats()
	print("Total missed packets: " .. tostring(stats.imissed))
	f:write(tostring(stats.ipackets+stats.imissed) .. " " .. tostring(stats.imissed) .. "\n")
	f:close()
end

function dumpSlave(rxDev, queue)
	local bufs = memory.bufArray()
	local pktCtr = stats:newPktRxCounter("Packets counted X", "plain")
	
	while lm.running() do		
		local rx = queue:tryRecv(bufs, 256)
		for i = 1, rx do
			local buf = bufs[i]
			pktCtr:countPacket(buf)
			hash_low = bit.band(buf.hash.rss, 0xFFFF)
			hash_high = bit.rshift(bit.band(buf.hash.rss, 0xFFFF0000), 16)
			
			if ft[hash_low][hash_high] ~= nil then
				ft[hash_low][hash_high] = ft[hash_low][hash_high] + 1
			else
				ft[hash_low][hash_high] = 1
			end
		end

		bufs:free(rx)		
		pktCtr:update()
	end

	pktCtr:finalize()
--[[
	for i = 0, FLOW_NUM-1 do
--		io.write("Flow entry " .. i .. ': ')

		for j,k in pairs(ft[i]) do
			io.write(j .. ' ' .. k .. ' ')
		end
--		io.write('\n')
	end
--]]
end
