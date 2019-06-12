Cache Architecture Analysis Tool
================================

This is a tool for analyzing the detailed structure of the cache even if
you do not know the cache architecture. The basic idea of this source code
is to analyze Cache Hierarchy, Cache size, Associativity and Cacheline size
according to memory access latency.

Some part of the source code comes from https://github.com/ob/cache.

* Test Enviromnent
   - Board : ZedBoard(Zynq7000)
   - OS : Bare-metal
   - SDK : Vivado 2018.3, Xilinx SDK 2018.3

* Generate Application in SDK (Xilinx SDK)
    1. Make new application (File > New > Application Project)
    2. Select the Empty Application templete and click Finish.
    3. Import cache.c file
 
* How to run this tool
    - After compilation, run this application, the following menu appears.
  	 
    ---------------------------------
	        1: run
         2: L1 Data cache (on)
         3: L2 Data Cache (on)
         4: fastmode
         5: Exit
    ---------------------------------

    - Select 1 or 4 to run the test (Normal or Fast mode).
    - If you want to run tests without L1 or L2 cache, select 2 or 3

* Test Result
    - After the application has measured all cache access latencies,
      it prints the expected cache structure as shown below.

    ---------------------------------------------------------------------
        L1 Cache is 32KB, 4-way (cacheline is 32B, latency 7.1 nsec)
        L2 Cache is 512KB, 8-way (cacheline is 32B, latency 38.4 nsec)
    ---------------------------------------------------------------------

