/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
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

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>

#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_thash.h>
#include <rte_byteorder.h>

#include <signal.h>
#include <time.h>

#include "main.h"
#include "murmur3.h"
#include "spooky.h"

static int
lcore_hello(__attribute__((unused)) void *arg)
{
	struct ipv4_5tuple tuple = {0,0,0,0,0};
	int hash;

	srand(10000);

	printf("Entering the main loop\n");

#ifdef TOE_BE
	rte_convert_rss_key((uint32_t *)&rss_hash_default_key, (uint32_t *)converted_key, RTE_DIM(rss_hash_default_key));
#endif

#ifdef MURMUR
	uint32_t murmur[4];
#endif

#ifdef BOB
	uint64_t h1, h2;
#endif

#ifdef INC
	//timespec_get(&ts, TIME_UTC);
        struct timespec t;
#endif

	while(1)
	{

#ifdef INC
	        timespec_get(&ts, TIME_UTC);
#endif

#ifdef TOE
		hash = rte_softrss((uint32_t *)&tuple, RTE_THASH_V4_L4_LEN, rss_hash_default_key);
#elif defined(TOE_BE)
		hash = rte_softrss_be((uint32_t *)&tuple, RTE_THASH_V4_L3_LEN, converted_key);
#elif defined(ADD)
		hash = tuple.proto + tuple.src_addr + tuple.dst_addr + tuple.sport + tuple.dport;
#elif defined(XOR)
		hash = tuple.proto ^ tuple.src_addr ^ tuple.dst_addr ^ tuple.sport ^ tuple.dport;
#elif defined(MURMUR)
		MurmurHash3_x64_128(&tuple, sizeof(tuple), 42, murmur);
#elif defined(BOB)
		spooky_hash128((uint32_t *)&tuple, sizeof(tuple), &h1, &h2);
#endif

#ifdef INC
                timespec_get(&t, TIME_UTC);
                tot_inter += 1.0*((t.tv_sec-ts.tv_sec)*1000000000 + t.tv_nsec - ts.tv_nsec)/1000000000;

		ctr++;

#elif defined(RAND)
		#ifdef MURMUR
		if (murmur[3]%2)
			ib[1]++;
		else
			ib[0]++;

		#elif defined(BOB)
		if (h2%2)
			ib[1]++;
		else
			ib[0]++;

		#else

		if (hash%2)
			ib[1]++;
		else
			ib[0]++;

		#endif
		ctr = rand();
#endif

		tuple.proto = rand();
		tuple.src_addr = rand();
		tuple.dst_addr = rand();
		tuple.sport = rand();
		tuple.dport = rand();
	}

	printf("%d\n",hash);
	return ctr;
}

static void handler(int sig)
{
	printf("\nSignal %d received\n", sig);

#ifdef INC
	printf("Total measurements: %lu, Duration %f, Hash rate: %lf\n", ctr, tot_inter, ctr/tot_inter);
#elif defined(RAND)
	printf("The loads: %lu %lu\n", ib[0], ib[1]);
#endif

	FILE *fp = fopen("tmp.txt", "a");
#ifdef INC
	fprintf(fp, "%lu %lf\n", ctr, ctr/tot_inter);
#elif defined(RAND)
	fprintf(fp, "%lu %lu\n", ib[0], ib[1]);
#endif
	fclose(fp);
	exit(1);
}

int main(void)
{
	signal(SIGINT, handler);

	lcore_hello(NULL);

	return 0;
}
