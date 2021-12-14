/*
 *
 *
 *   Copyright(c) 2017
 *	   	Telecom ParisTech LINCS
 *		Politecnico di Torino
 *
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * For bug report and other information please write to:
 * tianzhu.zhang@polito.it
 *
 *
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <sys/syscall.h>
#include <pthread.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_malloc.h>
#include <rte_timer.h>
#include <rte_prefetch.h>

#include "main.h"

static struct rte_timer timer;
static uint64_t re[RX_RINGS];

#ifdef IPG
static uint64_t global = 0;
#endif

struct rte_eth_rxconf rx_conf;
static struct rte_mempool *mbuf_pool[RX_RINGS];

#ifdef SD

#define gettid() syscall(__NR_gettid)

#define SCHED_DEADLINE 6

/* use the proper syscall numbers */
#ifdef __x86_64__
#define __NR_sched_setattr             314
#define __NR_sched_getattr             315
#endif

#ifdef __i386__
#define __NR_sched_setattr             351
#define __NR_sched_getattr             352
#endif

#ifdef __arm__
#define __NR_sched_setattr             380
#define __NR_sched_getattr             381
#endif

static volatile int done;

struct sched_attr {
       __u32 size;

       __u32 sched_policy;
       __u64 sched_flags;

       /* SCHED_NORMAL, SCHED_BATCH */
       __s32 sched_nice;

       /* SCHED_FIFO, SCHED_RR */
       __u32 sched_priority;

       /* SCHED_DEADLINE (nsec) */
       __u64 sched_runtime;
       __u64 sched_deadline;
       __u64 sched_period;
};

int sched_setattr(pid_t pid,
                 const struct sched_attr *attr,
                 unsigned int flags)
{
       return syscall(__NR_sched_setattr, pid, attr, flags);
}

int sched_getattr(pid_t pid,
                 struct sched_attr *attr,
                 unsigned int size,
                 unsigned int flags)
{
       return syscall(__NR_sched_getattr, pid, attr, size, flags);
}

#endif

/*
#define CPU_SET_NAME "DPDK-Flowcount"

static int current_core;
static int nb_istance = 3;

static void set_scheduling_policy(void)
{
        struct sched_attr sc_attr;
	int ret;

        uint64_t mask;
        char * cpu_set_name = strdup(CPU_SET_NAME);
        char dir_command [1000];

	mask = 0xFFFFFFFFFF;
	ret = sched_setaffinity(0, sizeof(mask), (cpu_set_t*)&mask);
	if (ret != 0) rte_exit(EXIT_FAILURE, "Error: cannot set affinity. Quitting...\n");

	system("mkdir -p /dev/cpuset; mount -t cpuset cpuset /dev/cpuset");
	cpu_set_name[10] += nb_istance/10;
	cpu_set_name[11] += nb_istance%10;
	sprintf(dir_command, "mkdir -p /dev/cpuset/%s", cpu_set_name);
	system(dir_command);
	sprintf(dir_command, "/bin/echo %d > /dev/cpuset/%s/cpuset.cpus", current_core, cpu_set_name);
	system(dir_command);
	sprintf(dir_command, "/bin/echo %d > /dev/cpuset/%s/cpuset.mems", rte_socket_id(), cpu_set_name);
	system(dir_command);
	sprintf(dir_command, "/bin/echo 0 > /dev/cpuset/cpuset.sched_load_balance; /bin/echo 1 > /dev/cpuset/cpuset.cpu_exclusive; ");
	system(dir_command);
	sprintf(dir_command, "/bin/echo %ld > /dev/cpuset/%s/tasks", syscall(SYS_gettid), cpu_set_name);
	system(dir_command);

        sc_attr.size = sizeof(struct sched_attr);
        sc_attr.sched_policy = SCHED_DEADLINE;
        sc_attr.sched_runtime =  SCHED_RUNTIME_NS;
        sc_attr.sched_deadline = SCHED_TOTALTIME_NS ;
        sc_attr.sched_period = SCHED_TOTALTIME_NS;
        ret = syscall(__NR_sched_setattr,0, &sc_attr, 0);
        if (ret != 0)
                rte_exit(EXIT_FAILURE, "Cannot set thread scheduling policy, Ret code=%d Errno=%d (EINVAL=%d ESRCH=%d E2BIG=%d EINVAL=%d E2BIG=%d EBUSY=%d EINVAL=%d EPERM=%d). Quitting...\n",ret, errno, EINVAL, ESRCH , E2BIG, EINVAL ,E2BIG, EBUSY, EINVAL, EPERM);
}
*/

const struct rte_fdir_conf fdir_conf = {
		.mode = RTE_FDIR_MODE_PERFECT,
		.pballoc = RTE_FDIR_PBALLOC_64K,
		.status = RTE_FDIR_REPORT_STATUS_ALWAYS,
		.mask = {
			.vlan_tci_mask = 0x0,
			.ipv4_mask = {
				.src_ip = 0,
				.dst_ip = 0,
			},
			.ipv6_mask = {
				.src_ip = {0,0,0,0},
				.dst_ip = {0,0,0,0},
			},
			.src_port_mask = 0,
			.dst_port_mask = 0,
			.mac_addr_byte_mask = 0,
			.tunnel_type_mask = 0,
			.tunnel_id_mask = 0,
		},
		.drop_queue = 63,
};

static const struct rte_eth_conf port_conf_default = {
        .rxmode = {
                .mq_mode = ETH_MQ_RX_RSS,
                .max_rx_pkt_len = ETHER_MAX_LEN,
                .split_hdr_size = 0,
                //.header_split   = 0, /**< Header Split disabled */
                //.hw_ip_checksum = 0, /**< IP checksum offload enabled */  //DISABLED!
                //.hw_vlan_filter = 0, /**< VLAN filtering disabled */
                //.jumbo_frame    = 1, /**< Jumbo Frame Support disabled */ // ENABLED!
                //.hw_strip_crc   = 0, /**< CRC stripped by hardware */

        },
        .rx_adv_conf = {
                .rss_conf = {
		    .rss_key = NULL,
                    .rss_hf = ETH_RSS_IPV4, //ETH_RSS_PROTO_MASK,
                }
        },
//	.fdir_conf = fdir_conf,
	.link_speeds = ETH_LINK_SPEED_10G,//ETH_LINK_SPEED_AUTONEG,
};

static void timer_cb(__attribute__((unused)) struct rte_timer *tim,
			__attribute__((unused)) void *arg)
{
	uint8_t i;
	double j = 0;
	static double old = 0;

	struct rte_eth_stats eth_stats;
	rte_eth_stats_get(PORT_ID, &eth_stats);

	struct timespec tstamp;

	// test the timestamping ability, may got removed in the future.
	if (!rte_eth_timesync_read_rx_timestamp(2, &tstamp, 0))
		puts("Timestamp detected...");

	for(i=0; i<RX_RINGS; i++)
		j += re[i];

	printf("RX rate: %.2lf Mpps, Total RX pkts: %.0lf, Total dropped pkts: %lu\n",
						 (j - old)/1000000, j, eth_stats.imissed);
	old = j;

	#ifdef IPG
		#ifdef LINKED_LIST
			printf("[IPG] Average IPG: %.0lf\n", flows[65246]->avg);
		#endif
		#ifdef DOUBLE_HASH
			printf("[IPG] Average IPG: %.0lf\n", pkt_ctr[65246].avg[0]);
		#endif
		#ifdef HASH_LIST
			printf("[IPG] Average IPG: %.0lf, stdDev %lf\n", pkt_ctr[65246].avg[0], pkt_ctr[65246].stdDev[0]);
		#endif
	#endif
}

#ifdef QUANTILE
static inline void get_qt(struct quantile *qt, uint64_t x)
{
	int i, k, d;
	double q1;

	if (x > qt->q[2])
	{
		if (x < qt->q[3])
			k = 2;
		else if (x >= qt->q[3] && x <= qt->q[4])
			k = 3;
		else
		{
			qt->q[4] = x;
			k = 3;
		}
	}
	else
	{
		if (x >= qt->q[1])
			k = 1;
		else if (qt->q[0] <= x && x < qt->q[1])
			k = 0;
		else
		{
			qt->q[0] = x;
			k = 0;
		}
	}

	// increment positions of markers k+1 through 5
	while (k<4)
	{
		qt->n[k+1]++;
		k++;
	}

	// Update desired positions for all markers
	qt->n1[0] += dn0;
	qt->n1[1] += dn1;
	qt->n1[2] += dn2;
	qt->n1[3] += dn3;
	qt->n1[4] += dn4;

	// Adjust heights of markers 2-4 if necessary
	for (i=1; i<=3; i++)
	{
		d = qt->n1[i] - qt->n[i];
		if ((d>=1 && qt->n[i+1] - qt->n[i]>1) || (d<=-1 && qt->n[i-1] - qt->n[i]<-1))
		{
			d = d>0? 1: -1;
			q1 = qt->q[i] + ((float)d/(qt->n[i+1] - qt->n[i-1])) *
				 	(float)((qt->n[i] - qt->n[i-1]+d)*(qt->q[i+1] - qt->q[i])/(qt->n[i+1] - qt->n[i])
					+ (qt->n[i+1]-qt->n[i]-d)*(qt->q[i]-qt->q[i-1])/(qt->n[i]-qt->n[i-1]));
			if (q1 > qt->q[i-1] && q1 < qt->q[i+1])
				qt->q[i] = q1;
			else
				qt->q[i] = qt->q[i] + d*(qt->q[i+d] - qt->q[i])/(qt->n[i+d]-qt->n[i]);

			//printf("d=%d index %d %f\n", d, i, (float)d/(n[i+1]-n[i-1]));

			qt->n[i] += d;
		}
	}

}

static inline void insertSort(double n[])
{
	int i, j, tmp;

	for(i=1; i<5; i++)
	{
		j = i;
		while (j>0 && n[j]<n[j-1])
		{
			tmp = n[j];
			n[j] = n[j-1];
			n[j-1] = tmp;

			j--;
		}
	}
}

static inline
void init(struct quantile *qt)
{
        qt->n[0] = 1;
        qt->n[1] = 2;
        qt->n[2] = 3;
        qt->n[3] = 4;
        qt->n[4] = 5;

        qt->n1[0] = 1;
        qt->n1[1] = 1 + 2*P;
        qt->n1[2] = 1 + 4*P;
        qt->n1[3] = 3 + 2*P;
        qt->n1[4] = 5;
}

#endif

#ifdef DOUBLE_HASH
static inline void
double_hash(int p)
{
	uint8_t port;
	uint32_t buf, index_h, index_l;

#ifdef IPG
	int64_t curr;
#endif

	unsigned lcore_id;
	lcore_id = rte_lcore_id();
	printf("Setting: core %u checks queue %d\n", lcore_id, p);

	// associate to the number of rx queues.
	int q = p;

	/* Run until the application is quit or killed. */
	for (;;) {
		port = PORT_ID;
		struct rte_mbuf *bufs[BURST_SIZE];

		const uint16_t nb_rx = rte_eth_rx_burst(port, q,
				bufs, BURST_SIZE);

		if (unlikely(nb_rx == 0))
			continue;

		#ifdef IPG
		unsigned qNum = RX_RINGS;
		global = 0;
		while (qNum > 0)
			global += re[--qNum];
		#endif

		re[q] += nb_rx;

#ifdef FLOW_LEVEL
		// Per packet processing
		for (buf = 0; buf < nb_rx; buf++)
		{
			index_l = bufs[buf]->hash.rss & 0xffff;
			index_h = (bufs[buf]->hash.rss & 0xffff0000)>>16;

			#ifdef TIMESTAMP
			uint64_t timestamp = bufs[buf]->timestamp;
			RTE_SET_USED(timestamp);
			#endif

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

					pkt_ctr[index_l].avg[0] =
						(pkt_ctr[index_l].avg[0] * (pkt_ctr[index_l].ctr[0] - 1) + curr)/pkt_ctr[index_l].ctr[0];

					//if (pkt_ctr[index_l].ctr[0] < 10000 && index_l == 65246)
					//	printf("%lf %lu %ld\n", pkt_ctr[index_l].avg[0], pkt_ctr[index_l].ctr[0], curr);

					pkt_ctr[index_l].ipg[0] = global;
					#endif
				}
				else if(pkt_ctr[index_l].hi_f2 == index_h)
				{
					pkt_ctr[index_l].ctr[1]++;

					#ifdef IPG
				        curr = global - 1 - pkt_ctr[index_l].ipg[1];
					pkt_ctr[index_l].avg[1] =
						((pkt_ctr[index_l].avg[1] * (pkt_ctr[index_l].ctr[1] - 1)) + curr)/(float)pkt_ctr[index_l].ctr[1];

					pkt_ctr[index_l].ipg[1] = global;
					#endif
				}
				else
					pkt_ctr[index_l].ctr[2]++;
			}
		}
#endif
		for (buf = 0; buf < nb_rx; buf++)
			rte_pktmbuf_free(bufs[buf]);
	}
}

#elif defined(LINKED_LIST)

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
linked_list(int p)
{
	uint8_t port;
	uint16_t buf;
	uint8_t hit;
	int64_t curr;

	unsigned lcore_id;
	lcore_id = rte_lcore_id();
	printf("Setting: core %u checks queue %d\n", lcore_id, p);

	// associate to the number of rx queues.
	int q = p;

	/* Run until the application is quit or killed. */
	for (;;) {
		port = PORT_ID;
		struct rte_mbuf *bufs[BURST_SIZE];

		const uint16_t nb_rx = rte_eth_rx_burst(port, q,
				bufs, BURST_SIZE);

		if (unlikely(nb_rx == 0))
			continue;

		re[q] += nb_rx;

		#ifdef IPG
		unsigned qNum = RX_RINGS;
		global = 0;
		while (qNum > 0)
			global += re[--qNum];
		#endif

		/* Per packet processing */
		for (buf = 0; buf < nb_rx; buf++)
		{
			hit = 0;

			uint32_t hash = bufs[buf] -> hash.rss;
			uint16_t hash_h = (hash & 0xffff0000) >> 16;
			uint16_t hash_l = hash & 0xffff;

			#ifdef TIMESTAMP
			uint64_t timestamp = bufs[buf]->timestamp;
			RTE_SET_USED(timestamp);
			#endif

			struct flow_entry *f = flows[hash_l], *tmp;

			while(likely(f != NULL))
			{
				if (f-> rss_high == 0)
				{
					f -> rss_high = hash_h;
					f -> ctr++;
					hit = 1;

					#ifdef IPG
					f -> avg = 0;//global - 1 - f->ipg;
					f -> ipg = 0;//global;
					#endif

					break;
				}
				else if (f -> rss_high == hash_h)
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

				f -> rss_high = hash_h;
				f -> ctr = 1;
				f -> next = NULL;
				tmp -> next = f;

				#ifdef IPG
				f -> avg = global - 1 - f->ipg;
				f -> ipg = global;
				#endif
			}

		}

		for (buf = 0; buf < nb_rx; buf++)
			rte_pktmbuf_free(bufs[buf]);
	}
}

#elif defined(HASH_LIST)

static inline void
hash_list(int p)
{
	uint8_t port;
	uint16_t buf, index_l, index_h;
	uint8_t hit;
	int64_t curr;
	int q = p;

#ifdef SD
	struct sched_attr attr;
	int ret;
	unsigned flags = 0;

        uint64_t mask;

        mask = 0xFFFFFFFFFF;
        ret = sched_setaffinity(0, sizeof(mask), (cpu_set_t*)&mask);
        if (ret != 0) rte_exit(EXIT_FAILURE, "Error: cannot set affinity. Quitting...\n");

        printf("deadline thread started [%ld]\n", gettid());

        attr.size = sizeof(attr);
        attr.sched_flags = 0;
        attr.sched_nice = 0;
        attr.sched_priority = 0;

        attr.sched_policy = SCHED_DEADLINE;
        attr.sched_runtime =  50 * 1000;
        attr.sched_period = attr.sched_deadline = 100 * 1000;

        ret = sched_setattr(0, &attr, flags);
        if (ret < 0) {
                done = 0;
                perror("sched_setattr");
                exit(-1);
        }
#endif

	unsigned lcore_id = rte_lcore_id();
	printf("Setting: core %u checks queue %d\n", lcore_id, q);

	struct rte_eth_stats eth_stats;
        rte_eth_stats_get(PORT_ID, &eth_stats);
	printf("Initial lost packets: %lu\n",eth_stats.imissed);

	struct flow_entry *f, *tmp;
	for (;;)
	{
		port = PORT_ID;
		struct rte_mbuf *bufs[BURST_SIZE];

		const uint16_t nb_rx = rte_eth_rx_burst(port, q,
				 bufs, BURST_SIZE);

		if (unlikely(nb_rx == 0))
		{
			//sched_yield();
			continue;
		}

		#ifdef IPG

		unsigned qNum = RX_RINGS;
		global = 0;
		while (qNum > 0)
			global += re[--qNum];

		#endif

		re[q] += nb_rx;

		//Per packet processing
		for (buf = 0; buf < nb_rx; buf++)
		{
			index_l = bufs[buf]->hash.rss & 0xffff;
			index_h = (bufs[buf]->hash.rss & 0xffff0000)>>16;

			rte_pktmbuf_free(bufs[buf]);

			#ifdef TIMESTAMP
			uint64_t timestamp = bufs[buf]->timestamp;
			if (timestamp)
				printf("[timestamp] %lu\n", timestamp);
			#endif

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
						(pkt_ctr[index_l].avg[0] * (pkt_ctr[index_l].ctr[0] - 1) + curr)/(float)pkt_ctr[index_l].ctr[0];
					pkt_ctr[index_l].ipg[0] = global;

					#ifdef QUANTILE
					if (pkt_ctr[index_l].qt[0].l < 5)
						pkt_ctr[index_l].qt[0].q[pkt_ctr[index_l].qt[0].l++] = curr;

					else
					{
						if (pkt_ctr[index_l].qt[0].l == 5)
							insertSort(pkt_ctr[index_l].qt[0].q);
						//printf("%lf\n", get_qt(pkt_ctr[index_l].qt[0], curr));
						get_qt(&pkt_ctr[index_l].qt[0], curr);
					}
					#endif

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

					#ifdef QUANTILE
                                        if (pkt_ctr[index_l].qt[1].l < 5)
                                                pkt_ctr[index_l].qt[1].q[pkt_ctr[index_l].qt[1].l++] = curr;

                                        else
                                        {
                                              if (pkt_ctr[index_l].qt[1].l == 5)
                                                        insertSort(pkt_ctr[index_l].qt[1].q);
                                                //printf("%lf\n", get_qt(pkt_ctr[index_l].qt[0], curr));
                                                get_qt(&pkt_ctr[index_l].qt[1], curr);
                                        }
                                        #endif

					#endif
				}
				else
				{
					rte_prefetch0(pkt_ctr[index_l].flows);
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

//		for (buf = 0; buf < nb_rx; buf++)
//			rte_pktmbuf_free(bufs[buf]);
	}
}

#endif

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int
port_init(uint8_t port)//, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf = port_conf_default;
	const uint16_t rx_rings = RX_RINGS, tx_rings = 0;
	int retval;
	uint16_t q;

        rx_conf.rx_thresh.pthresh = 8;
        rx_conf.rx_thresh.hthresh = 8;
        rx_conf.rx_thresh.wthresh = 0;
		rx_conf.rx_free_thresh = 2048;
        rx_conf.rx_drop_en = 0;

	#ifdef DOUBLE_HASH

	/* Initialize the corresponding data structure */
	uint32_t i;
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

	#elif defined(LINKED_LIST)

	/* Dynamical allocation for the linked list data structure */
	if (flow_struct_init(ENTRY_PER_FLOW) == -1)
	{
		printf("Flow struct initialization failure\n");
		return -1;
	}

	#elif defined(HASH_LIST)

        uint32_t i;
        int j;

        for (i=0; i< FLOW_NUM; i++)
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

	if (port >= rte_eth_dev_count())
		return -1;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	/* Allocate and set up RX_RINGS RX queues per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, RX_RING_SIZE,
				rte_eth_dev_socket_id(port), &rx_conf, mbuf_pool[q]);
		if (retval < 0)
			return retval;
	}

	/* Start the Ethernet port. */
	retval = rte_eth_dev_start(port);
	if (retval < 0)
		return retval;

	/* Display the port MAC address.*/
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

void handler(int sig)
{
	printf("\nSignal %d received\n", sig);

	struct rte_eth_stats eth_stats;
	rte_eth_stats_get(PORT_ID, &eth_stats);

	//puts("Stoping the device..\n");
        //rte_eth_dev_stop(PORT_ID);

	sleep(1);

	printf("\nBegin post processing...\n");
	int i = 0;

	#ifdef DOUBLE_HASH
	uint64_t sum = 0;

	for(i=0; i< FLOW_NUM; i++)
	{
		//printf("Flow %d\n", i);
		sum += pkt_ctr[i].ctr[2];
	}
	printf("\nThe total number of miscounted packets is %lu\n", sum);
	#endif

	#ifdef LINKED_LIST

	struct flow_entry *f;

	uint64_t sum = 0, fls = 0;
	for (i=0; i<FLOW_NUM; i++)
	{
		//printf("Flow entry %u: ", i);
		f = flows[i];

		while (f != NULL)
		{
			//printf("%u: %u  ", f->rss_high, f->ctr);
			sum += f->ctr;
			if (f -> ctr)
				fls += 1;
			f = f -> next;
		}

		//printf("\n");
	}

	printf("[Linked-list]: %lu flows with %lu packets\n", fls, sum);

	#endif

	#ifdef HASH_LIST

	uint64_t flows = 0, sum = 0;
	struct flow_entry *f;

	for (i = 0; i < FLOW_NUM; i++)
	{
//		printf("Flow entry %d: ", i);
		if (pkt_ctr[i].ctr[0] > 0)
		{
			flows+=1;
			sum += pkt_ctr[i].ctr[0];
		}

		if (pkt_ctr[i].ctr[1] > 0)
		{
			flows+=1;
			sum += pkt_ctr[i].ctr[1];
		}

//		printf("%u: %u  %u: %u  ", pkt_ctr[i].hi_f1, pkt_ctr[i].ctr[0], pkt_ctr[i].hi_f2, pkt_ctr[i].ctr[1]);
		f = pkt_ctr[i].flows;
		while (f != NULL)
		{
			if (f->ctr > 0)
				flows+=1;
			sum += f->ctr;
			//printf("%u: %u  ", f->rss_high, f->ctr);
			f= f->next;
		}
//		printf("\n");
	}

	printf("[Double Hash + Linked-list]: %lu flows with %lu packets\n", flows, sum);
	#endif

        printf("\nDPDK: Received pkts %lu \nDropped packets %lu \nErroneous packets %lu\n",
				eth_stats.ipackets + eth_stats.imissed + eth_stats.ierrors,
				eth_stats.imissed, eth_stats.ierrors);

	for(i=0; i<RX_RINGS; i++)
		printf("\nQueue %d counter's value: %lu\n", i, re[i]);

	#ifdef WRITE_FILE
	FILE *fp;
	fp = fopen("./tmp.txt", "a");
	fprintf(fp, "%lu %lu %lu %lu\n", eth_stats.ipackets + eth_stats.imissed
				 + eth_stats.ierrors, eth_stats.imissed, re[0], re[1]);
	fclose(fp);
	#endif

	exit(1);
}

/*
 * The main function, which does initialization and calls the per-lcore * functions.
 */
int
main(int argc, char *argv[])
{
//	struct rte_mempool *mbuf_pool;
	unsigned lcore_id;
	uint8_t portid, queueid;
	char s[64];

	signal(SIGINT, handler);

	/* Initialize the Environment Abstraction Layer (EAL). */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

	rte_timer_subsystem_init();

	rte_timer_init(&timer);

	uint64_t hz = rte_get_timer_hz();
	lcore_id = rte_lcore_id();
	rte_timer_reset(&timer, hz, PERIODICAL, lcore_id, timer_cb, NULL);

	argc -= ret;
	argv += ret;

	/* Creates a new mempool in memory to hold the mbufs. */
	//mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS*2,
	//	MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

	for(queueid=0; queueid<RX_RINGS; queueid++)
	{
		snprintf(s, sizeof(s), "mbuf_pool_%d", queueid);
		mbuf_pool[queueid] = rte_pktmbuf_pool_create(s, 4096,
		        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

		if (mbuf_pool[queueid] == NULL)
          	      rte_exit(EXIT_FAILURE, "Cannot create mbuf pool for queue %u\n", queueid);
	}

	//printf("Mempool size %d\n", RTE_MEMPOOL_CACHE_MAX_SIZE);

	//if (mbuf_pool == NULL)
	//	rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Initialize all ports. */
	portid = PORT_ID;
	if (port_init(portid) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu8 "\n", portid);

	int queue_id = 0;

	#ifdef DOUBLE_HASH
	RTE_LCORE_FOREACH_SLAVE(lcore_id){
		rte_eal_remote_launch((lcore_function_t *)double_hash, (void *)queue_id, lcore_id);
		if((queue_id)++ == RX_RINGS - 1)
			break;
	}

	#elif defined(LINKED_LIST)
	RTE_LCORE_FOREACH_SLAVE(lcore_id){
		rte_eal_remote_launch((lcore_function_t *)linked_list, (void *)queue_id, lcore_id);
		if((queue_id)++ == RX_RINGS - 1)
			break;
	}

	#elif defined(HASH_LIST)
        RTE_LCORE_FOREACH_SLAVE(lcore_id){
                rte_eal_remote_launch((lcore_function_t *)hash_list, (void *)queue_id, lcore_id);
                if((queue_id)++ == RX_RINGS - 1)
                        break;
        }

	#endif

	while(1)
		rte_timer_manage();
	return 0;
}
