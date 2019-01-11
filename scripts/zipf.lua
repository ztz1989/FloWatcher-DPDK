local mg		= require "moongen"
local memory		= require "memory"
local device		= require "device"
local stats		= require "stats"
local log 		= require "log"
local src = {}
local p = {}

function configure(parser)
	parser:description("Generates TCP SYN flood from varying source IPs, supports both IPv4 and IPv6")
	parser:argument("dev", "Devices to transmit from."):args("*"):convert(tonumber)
	parser:option("-r --rate", "Transmit rate in Mbit/s."):default(10000):convert(tonumber)
	parser:option("-i --ip", "Source IP (IPv4 or IPv6)."):default("10.0.0.1")
	parser:option("-d --destination", "Destination IP (IPv4 or IPv6).")
	parser:option("-f --flows", "Number of different IPs to use."):default(65536):convert(tonumber)
	parser:option("-h --hello",""):default("Hello World!")
end

function expand(a)
	for i, j in pairs(a) do
		for k = 1, j do table.insert(p,i) end
	end
	print(#p)
	return function()
		return p[math.random(1,#p)]
	end
end

function zipf(N, total, theta)
	sum = 0
	for i=1,N do
		sum = sum + 1.0/math.pow(i, 1-theta)
	end

	c = 1.0/sum

	for i=1,N do 
		src[i] = math.floor(total * c/math.pow(i+1,1-theta))
	end
end

function master(args)
	for i, dev in ipairs(args.dev) do
		local dev = device.config{port = dev}
		dev:wait()
		dev:getTxQueue(0):setRate(args.rate)
		mg.startTask("loadSlave", dev:getTxQueue(0), args.ip, args.flows, args.destination)
	end
	mg.waitForTasks()
end

function loadSlave(queue, minA, numIPs, dest)
        zipf(65536, 10e5, 0.1)
        f = expand(src)
--      print(f() .. ' ' .. f() .. ' ' .. f())

	--- parse and check ip addresses
	local minIP, ipv4 = parseIPAddress(minA)
	if minIP then
		log:info("Detected an %s address.", minIP and "IPv4" or "IPv6")
	else
		log:fatal("Invalid minIP: %s", minA)
	end

	-- min TCP packet size for IPv6 is 74 bytes (+ CRC)
	local packetLen = ipv4 and 60 or 74
	
	-- continue normally
	local mem = memory.createMemPool(function(buf)
		buf:getTcpPacket(ipv4):fill{ 
			ethSrc = queue,
			ethDst = "12:34:56:78:90:AB",
			ip4src = minIP,
			ip4Dst = dest, 
			ip6Dst = dest,
			tcpSyn = 1,
			tcpSeqNumber = 1,
			tcpWindow = 10,
			pktLength = packetLen,
			--ip4TTL = 111,
			--ip4TOS = 80
		}
	end)

	local bufs = mem:bufArray()
	local counter = 0
	local c = 0

	local txStats = stats:newDevTxCounter(queue, "plain")
	while mg.running() do
		-- fill packets and set their size 
		bufs:alloc(packetLen)
		for i, buf in ipairs(bufs) do 			
			local pkt = buf:getTcpPacket(ipv4)

			if ipv4 then
				pkt.ip4.src:set(minIP)
				pkt.ip4.src:add(f())				
				--pkt.tcp:setSrcPort(math.random(100))
				--pkt.tcp:setDstPort(math.random(10))
				--pkt.ip4.dst:add(math.random(256))		
			else
				pkt.ip6.src:set(minIP)
				pkt.ip6.src:add(counter)
			end
			--counter = incAndWrap(counter, numIPs)
		end

		--offload checksums to NIC
		bufs:offloadTcpChecksums(ipv4)
		
		queue:send(bufs)
		txStats:update()
	end
	txStats:finalize()
end

