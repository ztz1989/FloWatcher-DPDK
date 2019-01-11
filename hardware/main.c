/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2015 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <rte_malloc.h>

#define RX_RING_SIZE 4096

#define NUM_MBUFS 8192
#define MBUF_CACHE_SIZE 512
#define BURST_SIZE 256

#define RX_RINGS 2
#define PORT_ID 0

#define WRITE_FILE

#define SOFT

static const struct rte_eth_conf port_conf_default = {
        .rxmode = {
                .mq_mode = ETH_MQ_RX_RSS,
                .max_rx_pkt_len = ETHER_MAX_LEN,
                .split_hdr_size = 0,
                .header_split   = 0, /**< Header Split disabled */
                .hw_ip_checksum = 0, /**< IP checksum offload enabled */  //DISABLED!
                .hw_vlan_filter = 0, /**< VLAN filtering disabled */
                .jumbo_frame    = 1, /**< Jumbo Frame Support disabled */ // ENABLED!
                .hw_strip_crc   = 0, /**< CRC stripped by hardware */

        },
        .rx_adv_conf = {
                .rss_conf = {
			.rss_key = NULL,
                        .rss_hf = ETH_RSS_PROTO_MASK,
                }
        },
};

static inline void lcore_main(int);
void handler(int);

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int
port_init(uint8_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf = port_conf_default;
	const uint16_t rx_rings = RX_RINGS, tx_rings = 0;
	int retval;
	uint16_t q;

	if (port >= rte_eth_dev_count())
		return -1;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	/* Allocate and set up RX_RINGS RX queues per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, RX_RING_SIZE,
				rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}


	/* Start the Ethernet port. */
	retval = rte_eth_dev_start(port);
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct ether_addr addr;
	rte_eth_macaddr_get(port, &addr);
	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			(unsigned)port,
			addr.addr_bytes[0], addr.addr_bytes[1],
			addr.addr_bytes[2], addr.addr_bytes[3],
			addr.addr_bytes[4], addr.addr_bytes[5]);

	/* Enable RX in promiscuous mode for the Ethernet device. */
	rte_eth_promiscuous_enable(port);

	return 0;
}

/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port
 */
static inline void
lcore_main(int p)
{
	uint8_t port;
	uint16_t buf;

	unsigned lcore_id;
	lcore_id = rte_lcore_id();

#ifdef SOFT
	uint64_t i;
#endif
	printf("Setting: core %u checks queue %d\n", lcore_id, p);

	/* Run until the application is quit or killed. */
	for (;;) {
		port = PORT_ID;
		struct rte_mbuf *bufs[BURST_SIZE];

		const uint16_t nb_rx = rte_eth_rx_burst(port, p,
				bufs, BURST_SIZE);

		if (unlikely(nb_rx == 0))
			continue;

#ifdef SOFT
		i += nb_rx;
#endif
		for (buf = 0; buf < nb_rx; buf++)
			rte_pktmbuf_free(bufs[buf]);
	}
}

void handler(int sig)
{
	printf("\nSignal %d received\n", sig);

	// Get the statistics
	struct rte_eth_stats eth_stats;
	rte_eth_stats_get(PORT_ID, &eth_stats);
        printf("\nReceived pkts %" PRIu64 ", %lu, %lu\n", eth_stats.ipackets + eth_stats.imissed + eth_stats.ierrors, eth_stats.imissed, eth_stats.ierrors);

	puts("Stoping the device..\n");
        rte_eth_dev_stop(PORT_ID);

	sleep(1);

	#ifdef WRITE_FILE
	FILE *fp;
	fp = fopen("./tmp.txt", "a");
	fprintf(fp, "%lu %lu\n", eth_stats.ipackets + eth_stats.imissed + eth_stats.ierrors, eth_stats.imissed);
	fclose(fp);
	#endif

	exit(1);
}

/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int
main(int argc, char *argv[])
{
	struct rte_mempool *mbuf_pool;
	unsigned lcore_id;
	uint8_t portid;

	signal(SIGINT, handler);

	/* Initialize the Environment Abstraction Layer (EAL). */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

	argc -= ret;
	argv += ret;

	/* Creates a new mempool in memory to hold the mbufs. */
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

	//printf("\nRTE_MBUF_DEFAULT_BUF_SIZE %ld\n", RTE_MBUF_DEFAULT_BUF_SIZE);
	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Initialize all ports. */
	portid = PORT_ID;
	if (port_init(portid, mbuf_pool) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu8 "\n", portid);

	int queue_id = 0;

	//rte_eth_stats_get(PORT_ID, NULL);
	RTE_LCORE_FOREACH_SLAVE(lcore_id){
		rte_eal_remote_launch((lcore_function_t *)lcore_main, (void *)queue_id, lcore_id);
		if((queue_id)++ == RX_RINGS - 1)
			break;
	}

	while(1)
		;
	return 0;
}
