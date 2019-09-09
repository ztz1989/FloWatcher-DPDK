/*
 *
 *   Copyright(c) 2017
 *   		TeleCom ParisTech LINCS
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
 * 						tianzhu.zhang@polito.it
 *
 */

#define RX_RING_SIZE 4096
#define TX_RING_SIZE 512

#define WRITE_FILE   //option to write to a file

#define NUM_MBUFS 4096
#define MBUF_CACHE_SIZE 512
#define BURST_SIZE 256

#define RX_RINGS 1
#define PORT_ID 0

/* 3 data structure macros*/

#define DOUBLE_HASH
//#define LINKED_LIST
//#define HASH_LIST

//#define FLOW_LEVEL

/* Parameters for per-flow stats */
//#define IPG

//#define TIMESTAMP
//#define DEBUG_FLOW
//#define QUANTILE

//#define SD

#define FLOW_NUM 65536

#ifdef QUANTILE
	#define P 0.99

	#define dn0 0
	#define dn1 P/2
	#define dn2 P
	#define dn3 (P+1)/2
	#define dn4 1

        struct quantile
        {
                double q[5];
		int l;
                int n[5];
                float n1[5];
		//float dn[5];
        };

        //static inline void get_qt(struct quantile qt, double x);
	//static inline void insertSort(double n[]);
#endif

#ifdef DOUBLE_HASH
	static inline void double_hash(int);

	struct pkt_count
	{
        	uint16_t hi_f1;
	        uint16_t hi_f2;
	        uint32_t ctr[3];

		#ifdef IPG
		uint64_t ipg[2];
		double avg[2];
		#endif

	} __rte_cache_aligned;

	static struct pkt_count pkt_ctr[FLOW_NUM]__rte_cache_aligned;

#elif defined(LINKED_LIST)

	#define ENTRY_PER_FLOW 2
	static inline void linked_list(int);

	struct flow_entry {
		uint16_t rss_high;
		uint32_t ctr;
		struct flow_entry *next;

		#ifdef IPG
		uint64_t  ipg;
		double avg;
		#endif

	}__rte_cache_aligned;

	static struct flow_entry *flows[FLOW_NUM] __rte_cache_aligned;

#elif defined(HASH_LIST)
	#define ENTRY_PER_FLOW 1

	static inline void hash_list(int);

	struct flow_entry
	{
		uint16_t rss_high;
                uint32_t ctr;
                struct flow_entry *next;

		#ifdef IPG
                uint64_t ipg;
		double avg, stdDev;

		#ifdef QUANTILE
		struct quantile qt;
		#endif

		#endif

	}__rte_cache_aligned;

	struct pkt_count
	{
		uint16_t hi_f1;
		uint16_t hi_f2;
		uint32_t ctr[2];

		#ifdef IPG
                uint64_t ipg[2];
		double  avg[2], stdDev[2];

		#ifdef QUANTILE
                struct quantile qt[2];
                #endif

                #endif

		struct flow_entry *flows;

	}__rte_cache_aligned;

	static struct pkt_count pkt_ctr[FLOW_NUM]__rte_cache_aligned;
#endif

void handler(int sig);
