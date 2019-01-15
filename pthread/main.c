/*
 * This program aims at creating a customized full blown DPDK application
 */

#define _GNU_SOURCE
 
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#include <math.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_common.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_timer.h>
#include <rte_cycles.h>
#include <rte_ring.h>
#include <rte_malloc.h>
#include <rte_string_fns.h>
#include <rte_debug.h>
#include <rte_thash.h>
#include <rte_tcp.h>

#include "init.h"

/* Set of macros */
#define MBUF_CACHE_SIZE 512
#define RX_RINGS 2
#define RX_RING_SIZE 4096
#define PORT_ID 0
#define PORT_MASK 0x4
#define BURST_SIZE 256
#define MAX_RX_QUEUE_PER_PORT 128
#define MAX_RX_DESC 4096
#define RX_RING_SZ 65536
#define WRITE_FILE
#define MAX_LCORE_PARAMS 1024
#define MAX_RX_QUEUE_PER_LCORE 16
#define NB_SOCKETS 4
#define NB_MBUF 4096

int bits, b1;
uint32_t result = 0;

//#define SD

#ifdef SD
	#include "sched_deadline_init.h"
#endif

#include "flow_id.h"
#include "murmur3.h"
#include "spooky.h"
/* mask of enabled ports. */
uint32_t enabled_port_mask = 0x8;

/* number of rx queues, 2 by default. */
uint8_t nb_rxq = RX_RINGS;

/* number of rx ring descriptors, 4096 by default. */
uint16_t nb_rx_desc = RX_RING_SIZE;

/* batch size for packet fetch */
uint16_t burst_size = BURST_SIZE;

/* Set in promiscuous mode on by default. */
static unsigned promiscuous_on = 1;

/* Write the stats into a tmp file*/
static unsigned write_on = 0;

uint16_t n_rx_thread, n_tx_thread;

static struct rte_timer timer;

struct rte_eth_rxconf rx_conf;

static struct rte_mempool *pktmbuf_pool[NB_SOCKETS];

uint64_t *gCtr;

static struct rte_eth_conf port_conf = {
	.rxmode = {
		.mq_mode = ETH_MQ_RX_RSS,
		.max_rx_pkt_len = ETHER_MAX_LEN,
		.split_hdr_size = 0,
		.header_split   = 0, /**< Header Split disabled */
		.hw_ip_checksum = 0, /**< IP checksum offload enabled */
		.hw_vlan_filter = 0, /**< VLAN filtering disabled */
		.jumbo_frame    = 1, /**< Jumbo Frame Support disabled */
		.hw_strip_crc   = 0, /**< CRC stripped by hardware */
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = NULL,
			.rss_hf = ETH_RSS_PROTO_MASK,
		}
	},
};

enum {
	CMD_LINE_OPT_NB_RXQ_NUM = 256,
	CMD_LINE_OPT_NB_RX_DESC,
	CMD_LINE_OPT_BURST_SIZE,
	CMD_LINE_OPT_CONFIG,
	CMD_LINE_OPT_RX_CONFIG,
	CMD_LINE_OPT_TX_CONFIG,
	CMD_LINE_OPT_FILE
};

struct lcore_rx_queue {
	uint8_t port_id;
	uint8_t queue_id;
} __rte_cache_aligned;

struct rx_thread_params {
	uint8_t port_id;
	uint8_t queue_id;
	uint8_t lcore_id;
	uint8_t thread_id;
}__rte_cache_aligned;

static struct rx_thread_params rx_thread_params_array[MAX_LCORE_PARAMS];
static struct rx_thread_params rx_thread_params_array_default[] = {
	{3, 0, 2, 0},
	{3, 1, 4, 1},
};

static struct rx_thread_params *rx_thread_params =
		rx_thread_params_array_default;
static uint16_t nb_rx_thread_params = RTE_DIM(rx_thread_params_array_default);

struct tx_thread_params {
	uint8_t lcore_id;
	uint8_t thread_id;
} __rte_cache_aligned;

static struct tx_thread_params tx_thread_params_array[MAX_LCORE_PARAMS];
static struct tx_thread_params tx_thread_params_array_default[] = {
	{6, 0},
	{8, 1},
};

static struct tx_thread_params *tx_thread_params =
		tx_thread_params_array_default;
static uint16_t nb_tx_thread_params = RTE_DIM(tx_thread_params_array_default);

#define MAX_RX_QUEUE_PER_THREAD 16
#define MAX_TX_PORT_PER_THREAD  RTE_MAX_ETHPORTS
#define MAX_TX_QUEUE_PER_PORT   RTE_MAX_ETHPORTS
#define MAX_RX_QUEUE_PER_PORT   128

#define MAX_RX_THREAD 1024
#define MAX_TX_THREAD 1024
#define MAX_THREAD    (MAX_RX_THREAD + MAX_TX_THREAD)

rte_atomic16_t rx_counter;  /**< Number of spawned rx threads */
rte_atomic16_t tx_counter;  /**< Number of spawned tx threads */

struct thread_conf {
	uint16_t lcore_id;      /**< Initial lcore for rx thread */
	uint16_t cpu_id;        /**< Cpu id for cpu load stats counter */
	uint16_t thread_id;     /**< Thread ID */
};

struct thread_rx_conf {
	struct thread_conf conf;

	uint16_t n_rx_queue;
	struct lcore_rx_queue rx_queue_list[MAX_RX_QUEUE_PER_LCORE];

	uint16_t n_ring;        /**< Number of output rings */
	struct rte_ring *ring[RTE_MAX_LCORE];

} __rte_cache_aligned;

uint16_t n_rx_thread;
struct thread_rx_conf rx_thread[MAX_RX_THREAD];

struct thread_tx_conf {
	struct thread_conf conf;
	struct rte_ring *ring;

} __rte_cache_aligned;

uint16_t n_tx_thread;
struct thread_tx_conf tx_thread[MAX_TX_THREAD];

static void timer_cb(__attribute__((unused)) struct rte_timer *tim,
			__attribute__((unused)) void *arg)
{
	uint8_t i, portid, nb_ports;
	double j = 0;

	static double old = 0;
        nb_ports = rte_eth_dev_count();
        for (portid = 0; portid < nb_ports; portid++)
                if ((enabled_port_mask & (1 << portid)) != 0)
                        break;

	struct rte_eth_stats eth_stats;
	rte_eth_stats_get(portid, &eth_stats);

	struct timespec tstamp;

	/* test the timestamping ability. This is a placeholder for future time-related features. */
	if (!rte_eth_timesync_read_rx_timestamp(2, &tstamp, 0))
		puts("Timestamp detected...");

	for(i=0; i < n_rx_thread; i++)
		j += gCtr[i];

	printf("RX rate: %.2lf Mpps, Total RX pkts: %.0lf, Total dropped pkts: %lu\n",
						 (j - old)/1000000, j, eth_stats.imissed);

	old = j;

	#ifdef IPG
		#ifdef LINKED_LIST
			printf("[IPG] Average IPG: %.0lf\n", flows[65246]->avg);
		#endif
		#ifdef DOUBLE_HASH
			printf("[IPG] Average IPG: %.0lf, stdDev %lf\n", pkt_ctr[65246].avg[0], pkt_ctr[65246].stdDev[0]);
		#endif
		#ifdef HASH_LIST
			printf("[IPG] Average IPG: %.0lf, stdDev %lf\n", pkt_ctr[65246].avg[0], pkt_ctr[65246].stdDev[0]);
		#endif
	#endif

}

/* display usage */
static void print_usage(const char *prgname)
{
	printf("%s [EAL options] --\n"\
	        " -p  PORTMASK: hexadecimal bitmask of ports to configure\n"\
		" -P: promiscuous mode (Default on)\n"
	        " --nb_rxq: Rx queues\n"\
	        " --nb_rx_desc: The size of RX descriptors (default 4096)\n"\
		" --burst_size: The reception batch size (default 256)\n"\
		" [--rx (port,queue,lcore,thread)[,(port,queue,lcore,thread]]\n"
		" [--tx (lcore,thread)[,(lcore,thread]]\n",
	       prgname);
}

static int parse_portmask(const char *portmask)
{
	char *end = NULL;
	unsigned long pm;

	pm = strtoul(portmask, &end, 16);
	if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (pm == 0)
		return -1;

	return pm;
}

static int parse_rx_queue_nb(const char *rxq)
{
	char *end = NULL;
	unsigned long pm;

	pm = strtoul(rxq, &end, 10);
	if ((rxq[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (pm == 0)
		return -1;

	if (pm > MAX_RX_QUEUE_PER_PORT)
		pm = MAX_RX_QUEUE_PER_PORT;

	return pm;
}

static int parse_rx_desc_nb(const char *rxdesc)
{
	char *end = NULL;
	unsigned long pm;

	pm = strtoul(rxdesc, &end, 10);
	if ((rxdesc[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (pm == 0)
		return -1;

	if (pm > MAX_RX_DESC)
		pm = MAX_RX_DESC;

	return pm;
}


static int parse_rx_burst_size(const char *burst)
{
	char *end = NULL;
	unsigned long pm;

	pm = strtoul(burst, &end, 10);
	if ((burst[0] == '\0') || (end == NULL) || (*end != '\0'))
                return -1;

	if (pm == 0)
                return -1;

	return pm;
}

static int
parse_rx_config(const char *q_arg)
{
	char s[256];
	const char *p, *p0 = q_arg;
	char *end;
	enum fieldnames
	{
		FLD_PORT = 0,
		FLD_QUEUE,
		FLD_LCORE,
		FLD_THREAD,
		_NUM_FLD
	};
	unsigned long int_fld[_NUM_FLD];
	char *str_fld[_NUM_FLD];
	int i;
	unsigned size;

	nb_rx_thread_params = 0;

	while ((p = strchr(p0, '(')) != NULL)
	{
		++p;
		p0 = strchr(p, ')');
		if (p0 == NULL)
			return -1;

		size = p0 - p;
		if (size >= sizeof(s))
			return -1;

		snprintf(s, sizeof(s), "%.*s", size, p);
		if (rte_strsplit(s, sizeof(s), str_fld, _NUM_FLD, ',') != _NUM_FLD)
			return -1;

		for (i = 0; i < _NUM_FLD; i++)
		{
			errno = 0;
			int_fld[i] = strtoul(str_fld[i], &end, 0);
			if (errno != 0 || end == str_fld[i] || int_fld[i] > 255)
				return -1;
		}

		if (nb_rx_thread_params >= MAX_LCORE_PARAMS)
		{
			printf("exceeded max number of rx params: %hu\n",
					nb_rx_thread_params);
			return -1;
		}

		rx_thread_params_array[nb_rx_thread_params].port_id =
				(uint8_t)int_fld[FLD_PORT];
		rx_thread_params_array[nb_rx_thread_params].queue_id =
				(uint8_t)int_fld[FLD_QUEUE];
		rx_thread_params_array[nb_rx_thread_params].lcore_id =
				(uint8_t)int_fld[FLD_LCORE];
		rx_thread_params_array[nb_rx_thread_params].thread_id =
				(uint8_t)int_fld[FLD_THREAD];
		++nb_rx_thread_params;
	}
	rx_thread_params = rx_thread_params_array;
	return 0;
}

static int
parse_tx_config(const char *q_arg)
{
	char s[256];
	const char *p, *p0 = q_arg;
	char *end;
	enum fieldnames {
		FLD_LCORE = 0,
		FLD_THREAD,
		_NUM_FLD
	};
	unsigned long int_fld[_NUM_FLD];
	char *str_fld[_NUM_FLD];
	int i;
	unsigned size;

	nb_tx_thread_params = 0;

	while ((p = strchr(p0, '(')) != NULL) {
		++p;
		p0 = strchr(p, ')');
		if (p0 == NULL)
			return -1;

		size = p0 - p;
		if (size >= sizeof(s))
			return -1;

		snprintf(s, sizeof(s), "%.*s", size, p);
		if (rte_strsplit(s, sizeof(s), str_fld, _NUM_FLD, ',') != _NUM_FLD)
			return -1;
		for (i = 0; i < _NUM_FLD; i++) {
			errno = 0;
			int_fld[i] = strtoul(str_fld[i], &end, 0);
			if (errno != 0 || end == str_fld[i] || int_fld[i] > 255)
				return -1;
		}
		if (nb_tx_thread_params >= MAX_LCORE_PARAMS) {
			printf("exceeded max number of tx params: %hu\n",
				nb_tx_thread_params);
			return -1;
		}
		tx_thread_params_array[nb_tx_thread_params].lcore_id =
				(uint8_t)int_fld[FLD_LCORE];
		tx_thread_params_array[nb_tx_thread_params].thread_id =
				(uint8_t)int_fld[FLD_THREAD];
		++nb_tx_thread_params;
	}
	tx_thread_params = tx_thread_params_array;

	return 0;
}

/* parse the application arguments. */
static int parse_args(int argc, char **argv)
{
	int opt, ret;
	char **argvopt;
	char *prgname = argv[0];

	static struct option lgopts[] = {
		{"nb_rxq", 1, 0, CMD_LINE_OPT_NB_RXQ_NUM},
		{"nb_rx_desc", 1, 0, CMD_LINE_OPT_NB_RX_DESC},
		{"burst_size", 1, 0,  CMD_LINE_OPT_BURST_SIZE},
		{"write-file", 0, 0, CMD_LINE_OPT_FILE},
		{"rx_conf", 1, 0, CMD_LINE_OPT_RX_CONFIG},
		{"tx_conf", 1, 0, CMD_LINE_OPT_TX_CONFIG},
		{NULL, 0, 0, 0}
	};

	argvopt = argv;

	while ((opt = getopt_long(argc, argvopt, "p:P",
				lgopts, NULL)) != EOF)
	{
		switch (opt)
		{
			case 'p':
				enabled_port_mask = parse_portmask(optarg);
				if (enabled_port_mask == 0)
				{
					printf("Invalid port mask\n");
					print_usage(prgname);
					return -1;
				}
				printf("enabled port mask is %x\n", enabled_port_mask);
				break;

			case 'P':
				printf("Promiscuous mode enabled\n");
				promiscuous_on = 1;
				break;

			case CMD_LINE_OPT_NB_RXQ_NUM:
				ret = parse_rx_queue_nb(optarg);
				if (ret != -1)
				{
					nb_rxq = ret;
					printf("The number of rxq is %d\n", nb_rxq);
				}
				break;

			case CMD_LINE_OPT_NB_RX_DESC:
				ret = parse_rx_desc_nb(optarg);
				if (ret != -1)
				{
					nb_rx_desc = ret;
					printf("The number of RX descriptors is %d\n", nb_rx_desc);
				}
				break;

			case CMD_LINE_OPT_BURST_SIZE:
				ret = parse_rx_burst_size(optarg);
				if (ret != -1)
				{
					burst_size = ret;
					printf("The packet reception batch size is %d\n", burst_size);
				}
				break;

			case CMD_LINE_OPT_RX_CONFIG:
				ret = parse_rx_config(optarg);
				if (ret)
				{
					printf("invalid rx-config\n");
					print_usage(prgname);
					return -1;
				}
				break;

			case CMD_LINE_OPT_TX_CONFIG:
				ret = parse_tx_config(optarg);
				if (ret)
				{
					printf("invalid tx-config\n");
                                        print_usage(prgname);
                                        return -1;
				}
				break;
			case CMD_LINE_OPT_FILE:
				write_on = 1;
				break;

			default:
				return -1;
				print_usage(prgname);
		}
	}

	if (optind >= 0)
		argv[optind-1] = prgname;

	ret = optind - 1;
	optind = 1;
	return ret;
}

static int
init_rx_queues(void)
{
	uint16_t i, nb_rx_queue;
	uint8_t thread;

	n_rx_thread = 0;

	for (i = 0; i < nb_rx_thread_params; ++i) {
		thread = rx_thread_params[i].thread_id;
		nb_rx_queue = rx_thread[thread].n_rx_queue;

		if (nb_rx_queue >= MAX_RX_QUEUE_PER_LCORE) {
			printf("error: too many queues (%u) for thread: %u\n",
				(unsigned)nb_rx_queue + 1, (unsigned)thread);
			return -1;
		}

		rx_thread[thread].conf.thread_id = thread;
		rx_thread[thread].conf.lcore_id = rx_thread_params[i].lcore_id;
		rx_thread[thread].rx_queue_list[nb_rx_queue].port_id =
			rx_thread_params[i].port_id;
		rx_thread[thread].rx_queue_list[nb_rx_queue].queue_id =
			rx_thread_params[i].queue_id;
		rx_thread[thread].n_rx_queue++;

		if (thread >= n_rx_thread)
			n_rx_thread = thread + 1;

	}
	return 0;
}

static int
init_tx_threads(void)
{
	int i;

	n_tx_thread = 0;
	for (i = 0; i < nb_tx_thread_params; ++i) {
		tx_thread[n_tx_thread].conf.thread_id = tx_thread_params[i].thread_id;
		tx_thread[n_tx_thread].conf.lcore_id = tx_thread_params[i].lcore_id;
		printf("tx-thread %u: thread_id %u, lcore_id %u\n", n_tx_thread, tx_thread[n_tx_thread].conf.thread_id, tx_thread[n_tx_thread].conf.lcore_id);
		n_tx_thread++;
	}
	return 0;
}

static int
init_rx_rings(void)
{
	unsigned socket_io;
	struct thread_rx_conf *rx_conf;
	struct thread_tx_conf *tx_conf;
	unsigned rx_thread_id, tx_thread_id;
	char name[256];
	struct rte_ring *ring = NULL;

	for (tx_thread_id = 0; tx_thread_id < n_tx_thread; tx_thread_id++)
	{
		tx_conf = &tx_thread[tx_thread_id];
		printf("Connecting tx-thread %d with rx-thread %d\n",
			tx_thread_id, tx_conf->conf.thread_id);

		rx_thread_id = tx_conf->conf.thread_id;
		if (rx_thread_id > n_tx_thread)
		{
			printf("connection from tx-thread %u to rx-thread %u fails"
				"(rx-thread not defined)\n",tx_thread_id, rx_thread_id);
			return -1;
		}

		rx_conf = &rx_thread[rx_thread_id];
		socket_io = rte_lcore_to_socket_id(rx_conf->conf.lcore_id);

		snprintf(name, sizeof(name), "app_ring_s%u_rx%u_tx%u",
			socket_io, rx_thread_id, tx_thread_id);

		ring = rte_ring_create(name, RX_RING_SZ, socket_io,
			RING_F_SP_ENQ | RING_F_SC_DEQ);
		if (ring == NULL)
			rte_panic("Cannot create ring to connect rx-thread %u "
				"with tx-thread %u\n", rx_thread_id, tx_thread_id);

		rx_conf->ring[rx_conf->n_ring] = ring;

		tx_conf->ring = ring;
		rx_conf->n_ring++;
	}
	return 0;
}

/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port
 */
static inline void
lcore_main_rx(__attribute__((unused)) void *dummy)
{
	uint8_t port;
	//uint32_t buf;
	int q;

	unsigned lcore_id;
	struct thread_rx_conf *rx_conf;

	lcore_id = rte_lcore_id();
	rx_conf = (struct thread_rx_conf *)dummy;

	q = rx_conf->rx_queue_list[0].queue_id;
	port = rx_conf->rx_queue_list[0].port_id;

#ifdef SD
        set_affinity(13,20);
#endif

	printf("[Runtime settings]: lcore %u checks queue %d\n", lcore_id, q);

	for (;;)
	{
		struct rte_mbuf *bufs[burst_size];

		const uint16_t nb_rx = rte_eth_rx_burst(port, q,
				bufs, burst_size);

		if (unlikely(nb_rx == 0))
			continue;

		rte_ring_enqueue_burst(rx_conf->ring[0],
                                (void *)bufs, nb_rx, 0);
	}
}

/*
 * The lcore main. This is the main thread that does the per-flow statistics
 */
#ifdef HASH_LIST
static inline void
lcore_main_count_hash_list(__attribute__((unused)) void *dummy)
{
	unsigned lcore_id = rte_lcore_id(), q;
	uint32_t nb_rx, index_l, index_h;
	uint32_t buf;
	struct rte_mbuf *bufs[burst_size];
	struct thread_tx_conf *tx_conf = (struct thread_tx_conf *)dummy;
	uint8_t hit;

        struct ipv4_hdr *ipv4_hdr;
        struct tcp_hdr *tcp;

        union rte_thash_tuple ipv4_tuple;
	uint32_t hash;

	uint8_t default_rss_key[] = {
	        0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
	        0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
	        0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
	        0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
	        0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa,
	};

	uint8_t converted_rss_key[RTE_DIM(default_rss_key)];

	rte_convert_rss_key((uint32_t *)&default_rss_key, (uint32_t *)converted_rss_key, RTE_DIM(default_rss_key));

#ifdef IPG
	int64_t curr, global;
#endif

	q = tx_conf->conf.thread_id;
	printf("lcore %d dequeues queue %u\n", lcore_id, tx_conf->conf.thread_id);

	struct flow_entry *f, *tmp;

	for(;;)
	{
		//struct rte_mbuf *bufs[burst_size];
		nb_rx = rte_ring_dequeue_burst(tx_conf->ring, (void *)bufs, burst_size);
		if (unlikely(nb_rx == 0))
                        continue;

		gCtr[q] += nb_rx;

		#ifdef IPG
			unsigned qNum = n_rx_thread;
			global = 0;
			while (qNum > 0)
				global += gCtr[--qNum];
		#endif

		/* Per packet processing */
                for (buf = 0; buf < nb_rx; buf++)
                {
			ipv4_hdr = (struct ipv4_hdr *)(rte_pktmbuf_mtod(bufs[buf], struct ether_hdr *) + 1);
                        ipv4_tuple.v4.src_addr = rte_be_to_cpu_32(ipv4_hdr->src_addr);
                        ipv4_tuple.v4.dst_addr = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
                        //ipv4_tuple.proto = ipv4_hdr->next_proto_id;

                        tcp = (struct tcp_hdr *)((unsigned char *)ipv4_hdr + sizeof(struct ipv4_hdr));
                        ipv4_tuple.v4.sport = rte_be_to_cpu_16(tcp->src_port);
                        ipv4_tuple.v4.dport = rte_be_to_cpu_16(tcp->dst_port);
                        //printf("%u %u %u\n", ipv4_hdr->next_proto_id, ipv4_tuple.dport, ipv4_tuple.sport);

                        hash = rte_softrss_be((uint32_t *)&ipv4_tuple, RTE_THASH_V4_L3_LEN, converted_rss_key);
                        //hash = ipv4_tuple.v4.src_addr + ipv4_tuple.v4.dst_addr + ipv4_tuple.v4.sport + ipv4_tuple.v4.dport;
                        //hash = ipv4_tuple.v4.src_addr ^ ipv4_tuple.v4.dst_addr ^ ipv4_tuple.v4.sport ^ ipv4_tuple.v4.dport;
                        //MurmurHash3_x64_128(&ipv4_tuple, sizeof(ipv4_tuple), 1, &hash);
                        //hash = spooky_hash32(&ipv4_tuple,sizeof(ipv4_tuple), 1);

			rte_pktmbuf_free(bufs[buf]);

                        index_l = hash & 0xffff;
                        index_h = (hash & 0xffff0000) >> 16;

//			index_l = bufs[buf]->hash.rss & 0xffff;
//			index_h = (bufs[buf]->hash.rss & 0xffff0000)>>16;

//			rte_pktmbuf_free(bufs[buf]);

			if(pkt_ctr[index_l].hi_f1 == 0)
			{
				pkt_ctr[index_l].hi_f1 = index_h;
				pkt_ctr[index_l].ctr[0]++;

				#ifdef IPG
				pkt_ctr[index_l].avg[0] = pkt_ctr[index_l].ipg[0];
				#endif
			}
			else if(pkt_ctr[index_l].hi_f2 == 0 && pkt_ctr[index_l].hi_f1 != index_h)
			{
				pkt_ctr[index_l].hi_f2 = index_h;
				pkt_ctr[index_l].ctr[1]++;

				#ifdef IPG
				pkt_ctr[index_l].avg[1] = pkt_ctr[index_l].ipg[1];
				#endif
			}
			else
			{
				if(pkt_ctr[index_l].hi_f1 == index_h)
				{
					pkt_ctr[index_l].ctr[0]++;

					#ifdef IPG
					curr = global - 1 - pkt_ctr[index_l].ipg[0];
					pkt_ctr[index_l].stdDev[0] = sqrt(pow(curr - pkt_ctr[index_l].avg[0], 2));

					pkt_ctr[index_l].avg[0] =
						((pkt_ctr[index_l].avg[0] * (pkt_ctr[index_l].ctr[0] - 1)) + curr)/(float)pkt_ctr[index_l].ctr[0];

					pkt_ctr[index_l].ipg[0] = global;
					#endif
				}
				else if(pkt_ctr[index_l].hi_f2 == index_h)
				{
					pkt_ctr[index_l].ctr[1]++;

					#ifdef IPG
					curr = global - 1 - pkt_ctr[index_l].ipg[1];
	                                pkt_ctr[index_l].stdDev[1] = sqrt(pow(curr - pkt_ctr[index_l].avg[1], 2));
					pkt_ctr[index_l].avg[1] =
						(pkt_ctr[index_l].avg[1] * (pkt_ctr[index_l].ctr[1] - 1) + curr)/(float)pkt_ctr[index_l].ctr[1];

					pkt_ctr[index_l].ipg[1] = global;
					#endif
				}
				else
				{
					//rte_prefetch0(pkt_ctr[index_l].flows);
					f = pkt_ctr[index_l].flows;
					hit = 0;
					while (f != NULL)
					{
						if (f->rss_high == index_h)
						{
							f->ctr++;
							hit = 1;

							#ifdef IPG
							curr = global - 1 - f -> ipg;
							f -> stdDev = sqrt(pow(curr - (f -> avg), 2));
                                		        f -> avg = (f -> avg * (f -> ctr - 1) + curr)/(float)(f -> ctr);
                                        		#endif
							break;

						}
						else if (f->rss_high == 0)
                                                {
                                                        f->rss_high = index_h;
                                                        f->ctr++;
                                                        hit = 1;

                                                        #ifdef IPG
                                                        f -> avg = global - 1 - f->ipg;
                                                        f -> ipg = global;
                                                        #endif
                                                        break;
                                                }
						else
						{
							tmp = f;
							f = f-> next;
						}
					}

					// Online dynamic allocation.
					if (unlikely(hit == 0))
					{
						f = (struct flow_entry *)rte_calloc("Flow entry", 1,
                                        					sizeof(struct flow_entry), 0);
						if (unlikely(f == NULL))
                                		        rte_exit(EXIT_FAILURE, "flow bucket allocation failure\n");

						f->rss_high = index_h;
						f->ctr++;
						f->next = NULL;
						tmp->next = f;
					}
				}
			}
                }

	}
}
#endif

#ifdef LINKED_LIST

/* init flow_entry data structures for linked list, using dynamic allocation*/
static inline int
flow_struct_init(uint32_t size)
{
	uint32_t i, j;
	struct flow_entry *f= NULL;

	for(i=0; i<FLOW_NUM; i++)
	{
		flows[i] = (struct flow_entry *)rte_calloc("Flow entry", 1, 
					sizeof(struct flow_entry), 0);
		if (flows[i] == NULL)
		{
			printf("Flow entry allocation failed..\n");
			return -1;
		}

		flows[i] -> ctr = flows[i] -> rss_high = 0;
		flows[i] -> next = NULL;
		#ifdef IPG
		flows[i] -> ipg = flows[i] -> avg = 0;
		#endif
		f = flows[i];

		j = 0;
		while (j++ < size - 1)
		{
			f -> next = (struct flow_entry *)rte_calloc("Flow entry", 1,
					sizeof(struct flow_entry), 0);
			if (f == NULL)
			{
				printf("Flow bucket allocation failed..\n");
				return -1;
			}

			f = f -> next;
			f -> ctr = f -> rss_high = 0;
			#ifdef IPG
			f -> ipg = f -> avg = 0;
			#endif
			f -> next = NULL;
		}

	}
	return EXIT_SUCCESS;
}

static inline void
lcore_main_count_linked_list(__attribute__((unused)) void *dummy)
{
	unsigned lcore_id = rte_lcore_id(), q;
	uint32_t nb_rx, index_l, index_h;
	uint32_t buf;
	struct rte_mbuf *bufs[burst_size];
	struct thread_tx_conf *tx_conf = (struct thread_tx_conf *)dummy;
	uint8_t hit;

#ifdef SD
//      set_affinity(19,100);
#endif

#ifdef IPG
	int64_t curr, global;
#endif

	q = tx_conf->conf.thread_id;
	printf("lcore %d dequeues queue %u\n", lcore_id, tx_conf->conf.thread_id);

	//struct flow_entry *f, *tmp;

	/* Run until the application is quit or killed. */
	for (;;) {

		nb_rx = rte_ring_dequeue_burst(tx_conf->ring, (void *)bufs, burst_size);
		if (unlikely(nb_rx == 0))
			continue;

		gCtr[q] += nb_rx;

		#ifdef IPG
		unsigned qNum = n_rx_thread;
		global = 0;
		while (qNum > 0)
			global += gCtr[--qNum];
		#endif

		/* Per packet processing */
		for (buf = 0; buf < nb_rx; buf++)
		{
			hit = 0;

//			uint32_t hash = bufs[buf] -> hash.rss;
//			uint16_t hash_h = (hash & 0xffff0000) >> 16;
//			uint16_t hash_l = hash & 0xffff;

			index_l = bufs[buf]->hash.rss & result;
			index_h = (bufs[buf]->hash.rss & b1) >> bits;

			rte_pktmbuf_free(bufs[buf]);

			#ifdef TIMESTAMP
			uint64_t timestamp = bufs[buf]->timestamp;
			RTE_SET_USED(timestamp);
			#endif

			struct flow_entry *f = flows[index_l], *tmp;

			while(likely(f != NULL))
			{
				if (f-> rss_high == 0)
				{
					f -> rss_high = index_h;
					f -> ctr++;
					hit = 1;

					#ifdef IPG
					f -> avg = 0;//global - 1 - f->ipg;
					f -> ipg = 0;//global;
					#endif

					break;
				}
				else if (f -> rss_high == index_h)
				{
					f -> ctr++;
					hit = 1;

					#ifdef IPG
					curr = global - 1 - f -> ipg;
					f -> avg = (f -> avg * (f -> ctr - 1) + curr)/(float)(f -> ctr);
					f -> ipg = global;
					#endif

					break;
				}
				else
				{
					tmp = f;
					f = f -> next;
				}
			}

			if (unlikely(hit == 0))
			{
				//puts("Allocate new\n");
				f = (struct flow_entry *)rte_calloc("Flow entry", 1, sizeof(struct flow_entry), 0);

				if (unlikely(f == NULL))
					rte_exit(EXIT_FAILURE, "flow bucket allocation failure\n");

				f -> rss_high = index_h;
				f -> ctr = 1;
				f -> next = NULL;
				tmp -> next = f;

				#ifdef IPG
				f -> avg = global - 1 - f->ipg;
				f -> ipg = global;
				#endif
			}

		}

//		for (buf = 0; buf < nb_rx; buf++)
//			rte_pktmbuf_free(bufs[buf]);
	}
}

#endif

#ifdef DOUBLE_HASH
static inline void
lcore_main_count_double_hash(__attribute__((unused)) void *dummy)
{
	unsigned lcore_id = rte_lcore_id(), q;
	uint32_t nb_rx, index_l, index_h;
	uint32_t buf;
	struct rte_mbuf *bufs[burst_size];
	struct thread_tx_conf *tx_conf = (struct thread_tx_conf *)dummy;

	struct ipv4_hdr *ipv4_hdr;
        struct tcp_hdr *tcp;

        union rte_thash_tuple ipv4_tuple;

	struct ipv4_5tuple ip;
        uint32_t hash;

        uint8_t default_rss_key[] = {
                0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
                0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
                0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
                0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
                0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa,
        };

        uint8_t converted_rss_key[RTE_DIM(default_rss_key)]__rte_cache_aligned;

        rte_convert_rss_key((uint32_t *)&default_rss_key, (uint32_t *)converted_rss_key, RTE_DIM(default_rss_key));

#ifdef IPG
	int64_t curr, global;
#endif

	q = tx_conf->conf.thread_id;
	printf("lcore %d dequeues queue %u\n", lcore_id, tx_conf->conf.thread_id);

	//struct flow_entry *f, *tmp;

	for(;;)
	{
		//struct rte_mbuf *bufs[burst_size];
		nb_rx = rte_ring_dequeue_burst(tx_conf->ring, (void *)bufs, burst_size, 0);
		if (unlikely(nb_rx == 0))
                        continue;

		gCtr[q] += nb_rx;

//		for (buf = 0; buf < nb_rx; buf++)
//			rte_pktmbuf_free(bufs[buf]);

		#ifdef IPG
			unsigned qNum = n_rx_thread;
			global = 0;
			while (qNum > 0)
				global += gCtr[--qNum];
		#endif

        for (buf = 0; buf < nb_rx; buf++)
        {
/*          ipv4_hdr = (struct ipv4_hdr *)(rte_pktmbuf_mtod(bufs[buf], struct ether_hdr *) + 1);
            ipv4_tuple.v4.src_addr = rte_be_to_cpu_32(ipv4_hdr->src_addr);
            ipv4_tuple.v4.dst_addr = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
			ip.proto = ipv4_hdr->next_proto_id;
            ip.ip_src = rte_be_to_cpu_32(ipv4_hdr->src_addr);
			ip.ip_dst = rte_be_to_cpu_32(ipv4_hdr->dst_addr);

                        tcp = (struct tcp_hdr *)((unsigned char *)ipv4_hdr + sizeof(struct ipv4_hdr));
                        //ipv4_tuple.v4.sport = rte_be_to_cpu_16(tcp->src_port);
                        //ipv4_tuple.v4.dport = rte_be_to_cpu_16(tcp->dst_port);
       			ip.port_src = rte_be_to_cpu_16(tcp->src_port);
			ip.port_dst = rte_be_to_cpu_16(tcp->dst_port);
	                //printf("%u %u %u\n", ipv4_hdr->next_proto_id, ipv4_tuple.dport, ipv4_tuple.sport);

                        //hash = rte_softrss_be((uint32_t *)&ipv4_tuple, RTE_THASH_V4_L3_LEN, converted_rss_key);
                        //hash = ipv4_tuple.v4.src_addr + ipv4_tuple.v4.dst_addr + ipv4_tuple.v4.sport + ipv4_tuple.v4.dport;
                        //hash = ipv4_tuple.v4.src_addr ^ ipv4_tuple.v4.dst_addr ^ ipv4_tuple.v4.sport ^ ipv4_tuple.v4.dport;
                        //MurmurHash3_x64_128(&ipv4_tuple, sizeof(ipv4_tuple), 1, &hash);
                        //hash = spooky_hash32(&ipv4_tuple,sizeof(ipv4_tuple), 1);

			hash = rte_softrss_be((uint32_t *)&ip, 2, converted_rss_key);
//			hash = ip.ip_src + ip.ip_dst + ip.port_src + ip.port_dst + ip.proto;
//			hash = ip.ip_src ^ ip.ip_dst ^ ip.port_src ^ ip.port_dst ^ ip.proto;
//			MurmurHash3_x64_128(&ip, sizeof(ip), 1, &hash);
//			hash = spooky_hash32(&ip,sizeof(ip), 1);
                        rte_pktmbuf_free(bufs[buf]);

                        index_l = hash & 0xffff;
                        index_h = (hash & 0xffff0000) >> 16;

			//index_l = bufs[buf]->hash.rss & 0xffff;
*/			//index_h = (bufs[buf]->hash.rss & 0xffff0000)>>16;
			index_l = bufs[buf]->hash.rss & result;
			index_h = (bufs[buf]->hash.rss & b1)>>bits;

			rte_pktmbuf_free(bufs[buf]);

			if(pkt_ctr[index_l].hi_f1 == 0)
			{
				pkt_ctr[index_l].hi_f1 = index_h;
				pkt_ctr[index_l].ctr[0]++;

				#ifdef IPG
				pkt_ctr[index_l].avg[0] = pkt_ctr[index_l].ipg[0];
				#endif
			}
			else if(pkt_ctr[index_l].hi_f2 == 0 && pkt_ctr[index_l].hi_f1 != index_h)
			{
				pkt_ctr[index_l].hi_f2 = index_h;
				pkt_ctr[index_l].ctr[1]++;

				#ifdef IPG
				pkt_ctr[index_l].avg[1] = pkt_ctr[index_l].ipg[1];
				#endif
			}
			else
			{
				if(pkt_ctr[index_l].hi_f1 == index_h)
				{
					pkt_ctr[index_l].ctr[0]++;

					#ifdef IPG
					curr = global - 1 - pkt_ctr[index_l].ipg[0];
					pkt_ctr[index_l].stdDev[0] = sqrt(pow(curr - pkt_ctr[index_l].avg[0], 2));

					pkt_ctr[index_l].avg[0] =
						((pkt_ctr[index_l].avg[0] * (pkt_ctr[index_l].ctr[0] - 1)) + curr)/(float)pkt_ctr[index_l].ctr[0];

					pkt_ctr[index_l].ipg[0] = global;
					#endif
				}
				else if(pkt_ctr[index_l].hi_f2 == index_h)
				{
					pkt_ctr[index_l].ctr[1]++;

					#ifdef IPG
					curr = global - 1 - pkt_ctr[index_l].ipg[1];
	                                pkt_ctr[index_l].stdDev[1] = sqrt(pow(curr - pkt_ctr[index_l].avg[1], 2));
					pkt_ctr[index_l].avg[1] =
						(pkt_ctr[index_l].avg[1] * (pkt_ctr[index_l].ctr[1] - 1) + curr)/(float)pkt_ctr[index_l].ctr[1];

					pkt_ctr[index_l].ipg[1] = global;
					#endif
				}
			}

                }
	}
}
#endif

static void handler(int sig)
{
	int portid, i;

	printf("\nSignal %d received\n", sig);

        for (portid = 0; portid < rte_eth_dev_count(); portid++)
	        if ((enabled_port_mask & (1 << portid)) != 0)
                        break;

		printf("statistics for port %d:\n", portid);

		struct rte_eth_stats eth_stats;
		rte_eth_stats_get(portid, &eth_stats);

	        printf("[DPDK]  Received pkts %lu \n\tDropped packets %lu \n\tErroneous packets %lu\n",
				eth_stats.ipackets + eth_stats.imissed + eth_stats.ierrors,
				eth_stats.imissed, eth_stats.ierrors);

		uint64_t sum = 0;
 	        for(i=0; i<n_rx_thread; i++)
 	        {
			sum += gCtr[i];
	                printf("\nQueue %d counter's value: %lu\n", i, gCtr[i]);
	        }
		printf("\nValue of global counter: %lu\n", sum);

		sum = 0;
#ifdef HASH_LIST
		struct flow_entry *f;

		for(i=0; i<FLOW_NUM; i++)
		{
			if (pkt_ctr[i].ctr[0]>0)	sum+=1;
			if (pkt_ctr[i].ctr[1]>0)	sum+=1;

			f = pkt_ctr[i].flows;

			while (f != NULL)
			{
				if (f->ctr > 0)
					sum += 1;
				sum += f->ctr;
//				printf("%u: %u  ", f->rss_high, f->ctr);
				f= f->next;
			}
		}
#endif

#ifdef LINKED_LIST
		struct flow_entry *f;

		//uint64_t sum = 0, fls = 0;
		for (i=0; i<FLOW_NUM; i++)
		{
			//printf("Flow entry %u: ", i);
			f = flows[i];

			while (f != NULL && f->ctr > 0)
			{
				//printf("%u: %u  ", f->rss_high, f->ctr);
				sum += 1;
				//if (f -> ctr)
				//	fls += 1;
				f = f -> next;
			}

			//printf("\n");
		}

		//printf("[Linked-list]: %lu flows with %lu packets\n", fls, sum);
#endif

#ifdef DOUBLE_HASH
		for(i=0; i<FLOW_NUM; i++)
                {
                        if (pkt_ctr[i].ctr[0]>0)        sum+=1;
                        if (pkt_ctr[i].ctr[1]>0)        sum+=1;
/*			if (pkt_ctr[i].ctr[0]>0)
			{
				printf("[Flow entry %d] %d: %ld", i, pkt_ctr[i].hi_f1, pkt_ctr[i].ctr[0]);
				if (pkt_ctr[i].ctr[1]>0)
					printf("  %d: %ld",pkt_ctr[i].hi_f2, pkt_ctr[i].ctr[1]);
				putchar('\n');
			}
*/                }

#endif

		printf("\nThe total number of flows is %lu\n", sum);

	if (write_on)
	{
		FILE *fp;
		fp = fopen("./tmp.txt", "a");
		fprintf(fp, "%lu %lu %lu %lu %lu\n", eth_stats.ipackets + eth_stats.imissed + eth_stats.ierrors, eth_stats.imissed, gCtr[0], gCtr[1], sum);
		fclose(fp);
	}

	exit(1);
}

static int
pthread_run(__rte_unused void *arg)
{
	int lcore_id = rte_lcore_id();
	int i;

	for (i = 0; i < n_rx_thread; i++)
		if (rx_thread[i].conf.lcore_id == lcore_id)
		{
			printf("Start rx thread on %d...\n", lcore_id);
			lcore_main_rx((void *)&rx_thread[i]);
			return 0;
		}


	for (i = 0; i < n_tx_thread; i++)
		if (tx_thread[i].conf.lcore_id == lcore_id)
		{
			printf("Start tx thread on %d...\n", lcore_id);
#ifdef HASH_LIST
			lcore_main_count_hash_list((void *)&tx_thread[i]);
#endif

#ifdef LINKED_LIST
			lcore_main_count_linked_list((void *)&tx_thread[i]);
#endif

#ifdef DOUBLE_HASH
			lcore_main_count_double_hash((void *)&tx_thread[i]);
#endif

			return 0;
		}

	return 0;
}

int main(int argc, char **argv)
{
	//struct rte_mempool *mbuf_pool;
	unsigned lcore_id;
	uint32_t nb_lcores, nb_ports;
	uint8_t portid, qid;
	int i=0;
	char s[64];

	gCtr =  (uint64_t *)calloc(n_rx_thread, sizeof(int64_t));

	signal(SIGINT, handler);

	bits = log(FLOW_NUM)/log(2);
	while (i<bits)
	{
		result |= (1<<i);
		i++;
	}

	printf("Shift value %d\n", result);
	b1 = result << bits;

	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

	argc -= ret;
	argv += ret;

	/* parse the application arguments */
	printf("The configuration of %s\n", argv[0]);
	ret = parse_args(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Wrong APP parameters\n");

//	printf("Write file %u\n", write_on);

	printf("Initializing rx-queues...\n");
	ret = init_rx_queues();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "init_rx_queues failed\n");

	printf("Initializing tx-threads...\n");
	ret = init_tx_threads();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "init_tx_threads failed\n");

	printf("Initializing rings...\n");
	ret = init_rx_rings();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "init_rx_rings failed\n");

	rte_timer_subsystem_init();

	rte_timer_init(&timer);

	uint64_t hz = rte_get_timer_hz();
	lcore_id = rte_lcore_id();
	rte_timer_reset(&timer, hz, PERIODICAL, lcore_id, timer_cb, NULL);

/*	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", 9000,
			MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
*/
        rx_conf.rx_thresh.pthresh = 8;
        rx_conf.rx_thresh.hthresh = 8;
        rx_conf.rx_thresh.wthresh = 0;
	rx_conf.rx_free_thresh = 2048;
        rx_conf.rx_drop_en = 0;

	nb_lcores = rte_lcore_count();
	printf("The number of lcores is %u\n", nb_lcores);

	nb_ports = rte_eth_dev_count();
	printf("The number of ports is %u\n", nb_ports);

	for (portid = 0; portid < nb_ports; portid++)
	{
		if ((enabled_port_mask & (1 << portid)) == 0)
		{
			printf("\nSkip disabled port %d\n", portid);
			continue;
		}

		/* init ports */
		printf("\nInitialize port %d ...\n", portid);
		fflush(stdout);

		ret = rte_eth_dev_configure(portid, (uint16_t)n_rx_thread, 0, &port_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d,"
					"port=%d, rxq=%d\n", ret, portid, n_rx_thread);

		for (qid = 0; qid < n_rx_thread; qid++)
		{

			snprintf(s, sizeof(s), "mbuf_pool_%d", qid);
			pktmbuf_pool[qid] = rte_pktmbuf_pool_create(s, 5000,
                        	MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, 0);

        	        if (pktmbuf_pool[qid] == NULL)
        	                rte_exit(EXIT_FAILURE,
        	                       	"Cannot init mbuf pool for queue %d, %d\n", qid, rte_errno);
	                else
	                        printf("Allocated mbuf pool for queue %d\n", qid);

			ret = rte_eth_rx_queue_setup(portid, qid, nb_rx_desc,
					rte_eth_dev_socket_id(portid), &rx_conf, pktmbuf_pool[qid]);
			if (ret < 0)
				rte_exit(EXIT_FAILURE, "Cannot setup queue %d\n", qid);

/*			char ring_name[256];
			snprintf(ring_name, sizeof(ring_name), "transient_ring_%d", qid);
			rings[qid] = rte_ring_create(ring_name, RX_RING_SZ,
                        	rte_socket_id(), RING_F_SC_DEQ | RING_F_SP_ENQ);
			if (rings[qid] == NULL)
				rte_exit(EXIT_FAILURE, "Cannot create transient ring\n");
*/		}

		ret = rte_eth_dev_start(portid);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Cannot start port %d\n", portid);

		/* Display the port MAC address. */
		struct ether_addr addr;
		rte_eth_macaddr_get(portid, &addr);
		printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
				   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
				(unsigned)portid,
				addr.addr_bytes[0], addr.addr_bytes[1],
				addr.addr_bytes[2], addr.addr_bytes[3],
				addr.addr_bytes[4], addr.addr_bytes[5]);

		/* Enable RX in promiscuous mode for the Ethernet device. */
		if (promiscuous_on == 1)
			rte_eth_promiscuous_enable(portid);
	}
#ifdef DOUBLE_HASH
	/* Initialize the corresponding data structure */
	int j;

	for(i=0; i< FLOW_NUM; i++)
	{
		pkt_ctr[i].hi_f1 = pkt_ctr[i].hi_f2 = 0;
		for(j=0; j<=2; j++)
		{
			pkt_ctr[i].ctr[j] = 0;

			#ifdef IPG
			if (j == 2) break;
			pkt_ctr[i].avg[j] = pkt_ctr[i].ipg[j] = 0;
			#endif
		}
	}
#endif

#ifdef LINKED_LIST
	/* Dynamical allocation for the linked list data structure */
	if (flow_struct_init(ENTRY_PER_FLOW) == -1)
	{
		printf("Flow struct initialization failure\n");
		return -1;
	}

#endif

#ifdef HASH_LIST
	int j;
	for (i=0; i<FLOW_NUM; i++)
	{
		pkt_ctr[i].hi_f1 = pkt_ctr[i].hi_f2 = 0;

                for(j=0; j<2; j++)
                {
                        pkt_ctr[i].ctr[j] = 0;

                        #ifdef IPG
                        if (j == 2) break;
                        pkt_ctr[i].avg[j] = pkt_ctr[i].ipg[j] = 0;

			#ifdef QUANTILE
			init(&pkt_ctr[i].qt[j]);
			#endif

                        #endif
                }

		j = ENTRY_PER_FLOW;

		pkt_ctr[i].flows = (struct flow_entry *)rte_calloc("Flow entry", 1,
                        	                sizeof(struct flow_entry), 0);

		struct flow_entry *f = pkt_ctr[i].flows;

		while (--j > 0)
		{
			f -> next = (struct flow_entry *)rte_calloc("Flow entry", 1,
                	                        sizeof(struct flow_entry), 0);
			f = f -> next;
			f->next = NULL;
		}
	}
#endif
	rte_eal_mp_remote_launch(pthread_run, NULL, SKIP_MASTER);

	while(1)
		rte_timer_manage();

	return EXIT_SUCCESS;
}

