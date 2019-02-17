# FloWatcher-DPDK
FloWatcher-DPDK is a software traffic monitor with fine-grained statistics:
 a pure DPDK application written in C language, it provides detailed statistics on both packet- and flow-level. 
 
## At a glance
 In a nutshell, FloWatcher-DPDK achieves per-flow traffic monitoring at 14.88Mpps (ie 10 Gpbs line rate fully loaded  with minimum size  packets) with negligible packet losses (just a few packets per million) using a minimal amount of CPU resources.
 
To achieve this peformance, we leverage memory aligned double-hash (resolving collision with chaining) and avoid costly operations such as hash functions by leveraging RSS hash computed by the NIC and recently made available in DPDK.

We will be releasing a detailed technical report of FloWatcher-DPDK shortly.
 
 
## Capabilities
Specifically, FloWatcher-DPDK calculates:
* per-flow size, 
* per-flow throughput and 
* per-flow burstiness (number of packets of other flows in bewteen two packets of the same flow). 

Additionally, it can report:
* simple moments (i.e., average and standard deviation) as well as 
* computing more complex statistics such as per-flow percentiles of the above metrics (with a custom implementation of the PSQuare algorithm).

## Parameters and settings
FloWatcher-DPDK follows all the design priciples of DPDK applications. Specifically, we devise three versions according to the DPDK programming models and multi-threading models. 
* The first version is based on the run-to-completion model, its source code can be found inside the run-to-completion/ directory.
* The second version is based on pipeline model, using the traditional POSIX pthread, its source code is located in the pipeline/ directory.
* The third version is also based on pipeline model, but using the novel DPDK L-thread, its source code is inside the cooperative/ directory.

## Compilation instructions 
To compile, just like most of DPDK applications, enter the corresponding directory and type 'make'

## Usage instructions
The commands to run these models are not identical for now. In particular:

1, Run-to-completion model: Please refer to the README file of FloWatcher-DPDK-sample application. (https://github.com/ztz1989/FloWatcher-DPDK/tree/master/run_to_completion)

2, pthread pipeline model and L-thread pipeline model: Enter the corresponding sub-directory and conpile each of them using "make". The command to use is: 

sudo ./build/FloWatcher-DPDK -l 0,2,4,6,8 -- -P -p 8 --rx="(3,0,2,0)(3,1,4,1)" --tx="(6,0)(8,1)"

* --rx: (Port id, queue id, lcore id, logical rx-thread-id)

* --tx: (lcore id, logical monitor-thread-id)

* -P: Enable promiscuous mode, enabled by default

* -p: the port mask. Must match the port id specified by "--rx" options

* --write-file: If specified, write the statistics into a "tmp.txt" file under the same directory.

The same thread-id identifies a pair of rx-thread and monitor-thread, and both of them will enqueue/dequeue the same software ring.

More information on usage coming, please hold your breath!!
 
