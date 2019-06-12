/*
 * cache_latency_bench function modified from https://github.com/ob/cache
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/types.h>

#include "xscutimer.h"
#include "xuartps_hw.h"
#include "xil_cache_l.h"
#include "xil_cache.h"


/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/
#define	SAMPLE			(10)
#define	CACHE_MIN		(2 * 1024)		    // 2K
#define	CACHE_MAX		(2 * 1024 * 1024)	// 2MB!
#define TIMER_FREQ 		(333000000) 		// 330MHz
#define ONE_MILISECOND 	(333000) 			// 330KHz
#define ONE_MICROSECOND (333)   			// 333Hz
#define MAX_LOG 200
#define CACHE_LATENCY_GAP	(1.5)
#define ABS(a, b) ((a > b) ? (a - b) : (b - a))
#define WITHIN_RANGE(a, b, r) ((ABS(a, b) < r) ? 1 : 0)
#define ONE_SECOND 1000000

/****************************************************************************
 * Private Data
 ****************************************************************************/
static int e_time = ONE_SECOND;
static int status_l1 = 1;
static int status_l2 = 1;
static XScuTimer Timer;
static XScuTimer_Config *config;

static u32* buffer[CACHE_MAX];
static int lcnt;
static char buf[100];
struct perf_log {
	int stride;
	int csize;
	double perf;
} g_log[MAX_LOG];

/****************************************************************************
 * Private Functions
 ****************************************************************************/
static u32 timestamp(void)
{
	u32 val;

	val = XScuTimer_GetCounterReg(config->BaseAddr) / ONE_MICROSECOND;

	return val;
}

static void init_timer(void)
{
	config = XScuTimer_LookupConfig(XPAR_PS7_SCUTIMER_0_DEVICE_ID);

	XScuTimer_CfgInitialize(&Timer, config, config->BaseAddr);
	XScuTimer_LoadTimer(&Timer, 0xFFFFFFFF);
	XScuTimer_Start(&Timer);
}

static u32 ** linear(u32 **buffer, u32 stride, u32 max)
{
	u32 i, last = 1;

	for (i = stride + 1; i <= max; i += stride) {
		buffer[last] = (u32 *)&buffer[i];
		last = i;
	}
	buffer[max] = 0;
	return &buffer[1];
}

static char *print_size(int size)
{
	memset(buf, 0, sizeof(buf));

	if (size < 1024) sprintf(buf, "%dB", size);
	else if (size < 1024 * 1024) sprintf(buf, "%dKB", size/1024);
	else if (size < 1024 * 1024 * 1024) sprintf(buf, "%dMB", size/(1024*1024));
	else printf("overflow\n");

	return buf;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
void cache_result(void)
{
	int t_cnt = lcnt;
	int i, j = 0, k = 0;
	double average[10] = {0, };
	int csize[10] = {0, };

	int prev_csize = g_log[0].csize;
	double sum = 0;

	printf("[Test Done] ==========================================================\n");

	/*
	 * 1. Cache size : Based on latency
	 */
	for (i = 0; i < t_cnt; i++)
	{
		if (g_log[i].stride == 0) {
			break;
		} else if (prev_csize == g_log[i].csize) {
			sum += g_log[i].perf;
			j++;
		} else {
			csize[k] = g_log[i - 1].csize;
			average[k++] = sum / j;
			sum = g_log[i].perf;
			j = 1;
		}
		prev_csize = g_log[i].csize;
	}

	int cache_hierarchy = 1;
	int cache_size[4] = {0, };
	double cache_latency[4] = {0, };

	for (i = 0; i < k; i++) {
		if (average[i + 1] == 0) {
			break;
		} else if (average[i] * CACHE_LATENCY_GAP < average[i + 1]) {
			cache_latency[cache_hierarchy] = average[i];
			cache_size[cache_hierarchy++] = csize[i];
		} else {

		}
	}

	if (cache_hierarchy == 1) {
		printf("There is no Cache at all\n");
		return;
	}

	/* 2. Find associativity */
	int cache_level = 1;
	while(cache_hierarchy > cache_level) {
		int t_graph = cache_size[cache_level] * 2;
		int c_graph = cache_size[cache_level];
		double prev_cperf = 0;
		int p_way = 0, cache_line = 0xFFFF;

		for (j = 0; j < k; j++) {
			if (csize[j] == c_graph) {
				prev_cperf = average[j];
				break;
			}
		}

		for (i = 0; i < t_cnt; i++) {
			if (g_log[i].csize < t_graph)
				continue;

			if (g_log[i].csize > t_graph)
				break;

			/* 3. Calculate cache line size (10% relative difference) */
			if ((g_log[i].perf < g_log[i + 1].perf) &&
					(! (cache_line * 2 < g_log[i + 1].stride)) &&
					(!WITHIN_RANGE(g_log[i].perf, g_log[i + 1].perf, g_log[i + 1].perf / 10))) {
				cache_line = g_log[i + 1].stride;

			}

			/* Calculate associativity */
			if ((g_log[i].perf > g_log[i + 1].perf) &&
					(WITHIN_RANGE(g_log[i + 1].perf, prev_cperf, prev_cperf / 4))) {
				p_way = g_log[i + 1].csize / g_log[i + 1].stride;
				break;
			}
		}

		printf("\tL%d Cache is %s, %d-way (cacheline is %dB, latency %3.1lf nsec)\n",
			cache_level, print_size(cache_size[cache_level]), p_way, cache_line, cache_latency[cache_level]);
		cache_level++;
	}
	printf("======================================================================\n");
	return;
}

void cache_latency_bench(u32 cache_max)
{
	volatile u32 i, stride;
	u32 steps, csize, limit;
	u32 sec0, sec;
	u32 **start;
	register u32 **p;

	for (csize=CACHE_MIN; csize <= cache_max/4; csize*=2) {
		for (stride=1; stride <= csize/2; stride=stride*2) {
			Xil_DCacheFlush();
			init_timer();

			sec = 0.0;
			limit = csize - stride + 1;
			steps = 0;
			start = linear(buffer, stride, limit);

			do {
				sec0 = timestamp();
				for (i = SAMPLE * stride; i > 0; i--)
					for (p = start; p; p = (u32 **)*p) ;

				steps++;
				sec += sec0 - timestamp();
			} while (sec < e_time); /* one second */

			double perf = sec * 1e3/(steps * SAMPLE * stride * ((limit-1)/stride+1));

			printf("%9lu %9lu %9.1lf nsec\n", stride * 4, csize * sizeof(u32*), perf);

			/* Storing latency, stride and csize to estimate later */
			g_log[lcnt].stride = stride * 4;
			g_log[lcnt].csize = csize * 4;
			g_log[lcnt++].perf = perf;
		}
		printf("\n");
	}
	return;
}

void init_global_variable(void)
{
	lcnt = 0;
	for (int i; i < MAX_LOG; i++)
	{
		g_log[i].stride = 0;
		g_log[i].csize = 0;
		g_log[i].perf = 0;
	}
}

int main(void)
{
	int a;

Start:
	init_global_variable();

	printf("-----------------------------------------\n");
	printf("              Hello :)                   \n");
	printf("                                         \n");
	printf("\t1: run\n\t2: L1 Data cache (%s)\n\t3: L2 Data Cache (%s)\n\t4: fastmode\n\t5: Exit\n",
			status_l1? "on":"off", status_l2? "on":"off");
	printf("                                         \n");
	printf("-----------------------------------------\n\n");

	while(1) {
		a = XUartPs_RecvByte(XPS_UART1_BASEADDR);

		/* Parsing 0 ~ 9 */
		if ('0' <= a && a <= '9') {
			printf("%c", a);
			a -= '0';
			break;
		}
	}

	switch(a) {
		case 1:	/* Normal Test */
			printf("\n-- Test Started -- \n\t(L1 %s, L2 %s)\n", status_l1? "ON": "OFF", status_l2? "ON" : "OFF");
			e_time = ONE_SECOND;
			cache_latency_bench(CACHE_MAX);
			cache_result();
			printf("\n-- Test Finished -- \n");
			break;
			
		case 2: /* L1 Cache On/OFF */
			if (status_l1) {
				Xil_L1DCacheDisable();
			} else {
				Xil_L1DCacheEnable();
			}
			printf("\nL1 Cache %s\n", status_l1? ("on->off"):("off->on"));
			status_l1 ^= 1;
			goto Start;
			
		case 3: /* L2 Cache On/Off */
			if (status_l2) {
				Xil_L2CacheDisable();
			} else {
				Xil_L2CacheEnable();
			}
			printf("\nL2 Cache %s\n", status_l2? ("on->off"):("off->on"));
			status_l2 ^= 1;
			goto Start;
			
		case 4: /* Fast mode test */
			printf("\n-- Test Started [fastmode] -- \n\t(L1 %s, L2 %s)\n", status_l1? "ON": "OFF", status_l2? "ON" : "OFF");
			e_time = 100000;
			cache_latency_bench(CACHE_MAX);
			cache_result();
			printf("\n-- Test Finished -- \n");
			break;
			
		case 5:
			printf("\n-- Byebye -- \n");
			return 0;
			
		default:
			goto Start;
	}
	goto Start;
	return 0;
}
