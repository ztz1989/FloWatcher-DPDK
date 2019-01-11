#define DOUBLE_HASH
//#define LINKED_LIST
//#define HASH_LIST

#define IPG

#define ENTRY_PER_FLOW 1
#define FLOW_NUM 65536

#ifdef DOUBLE_HASH
	struct pkt_count
	{
        	uint16_t hi_f1;
	        uint16_t hi_f2;
	        uint64_t ctr[3];

		#ifdef IPG
		uint64_t ipg[2];
		double avg[2], stdDev[2];
		#endif

	} __rte_cache_aligned;

	static struct pkt_count pkt_ctr[FLOW_NUM]__rte_cache_aligned;

#endif

#ifdef LINKED_LIST
	struct flow_entry {
		uint16_t rss_high;
		uint64_t ctr;
		struct flow_entry *next;

		#ifdef IPG
		uint64_t  ipg;
		double avg, stdDev;
		#endif

	}__rte_cache_aligned;

	static struct flow_entry *flows[FLOW_NUM] __rte_cache_aligned;
#endif

#ifdef HASH_LIST
struct flow_entry
	{
		uint16_t rss_high;
                uint64_t ctr;
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
	uint64_t ctr[2];

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
