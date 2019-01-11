# FlowMon-DPDK
A DPDK software traffic monitor for per-flow statistics. We aim at providing the detailed statistics on both packet level and flow level. Specifically, FlowMon-DPDK aims at providing per-flow throughput, inter-packet gap and percentiles. 

## About the APP: 
  * Adopted aversion of DPDK: 17.05 
  * We use 2 RSS RX queues for packet reception. 
  * The program adopts both run-to-completion mode and pipeline mode of DPDK.
  * Suggested tunings: disable hyper-threading, use the "performance" CPU frequency governor, disable turbo-boost. For more information, refer to: https://wiki.fd.io/view/VPP/How_To_Optimize_Performance_(System_Tuning)
  
## Usage:
 * compile with "make", just like any DPDK application
 * the default RX port is 2, we can change it by editing the #RX_RINGS macro. Better interface for setting the parameters will be provided very soon.
 * to run it: sudo ./build/Flowcount -c 111
  
  "111" is just an example, we can choose any 3 lcores available.
  
