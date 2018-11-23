/** \file
 * \brief Example code for Simple Open EtherCAT master
 *
 * Usage : simple_test [ifname1]
 * ifname is NIC interface, f.e. eth0
 *
 * This is a minimal test.
 *
 * (c)Arthur Ketels 2010 - 2011
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sched.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <math.h>

#include <oshw.h>
#include "ethercat.h"

#define BIT(nr)		(1UL << (nr))

#define NSEC_PER_SEC 1000000000
#define EC_TIMEOUTMON 500

#define PL_PRINT_IP_REQUIRED	1
#define PL_PRINT_OP_REQUIRED	1
#define PL_DC_TEST		1

typedef void (*sighandler_t)(int);

static char IOmap[4096];
static OSAL_THREAD_HANDLE thread1;
static volatile int expectedWKC;
static volatile boolean needlf = FALSE;
static volatile int wkc;
static boolean inOP = FALSE;
static uint8 currentgroup = 0;
static unsigned long long loop = 0;

static boolean slave1_microchip_lan9252 = TRUE;

#ifdef PL_DC_TEST
static int64 toff, gl_delta;
static volatile int dorun = 0;
static volatile int deltat, tmax = 0;
#endif

#define MODULE_INDEX_LED_D24	8

static inline void set_output_bit (uint16 slave_no, uint8 module_index, uint8 value)
{
	*(ec_slave[slave_no].outputs + module_index) = value;
}

static void simpletest(char *ifname)
{
	int i, j, oloop = 0, iloop = 0, chk, cnt;
	needlf = FALSE;
	inOP = FALSE;

	printf("Starting simple test\n");

	/* initialise SOEM, bind socket to ifname */
	if (!ec_init(ifname)) {
		printf("No socket connection on %s\nExcecute as root\n",ifname);
		return;
	}

	printf("ec_init on %s succeeded.\n",ifname);

	/* find and auto-config slaves */
	if (ec_config(FALSE, &IOmap) <= 0) {
		 printf("No slaves found!\n");
		 goto exit_simple;
	}
	printf("%d slaves found and configured.\n",ec_slavecount);
	printf("DC capable : %d\n", ec_configdc());

	/* wait for all slaves to reach SAFE_OP state */
	ec_statecheck(0, EC_STATE_SAFE_OP,  EC_TIMEOUTSTATE * 4);

	if (ec_slave[0].state != EC_STATE_SAFE_OP) {
		printf("Not all slaves reached safe operational state.\n");
		ec_readstate();
		for(i = 1; i<=ec_slavecount ; i++) {
			if(ec_slave[i].state != EC_STATE_SAFE_OP) {
				printf("Slave %d State=%2x StatusCode=%4x : %s\n",
				i, ec_slave[i].state, ec_slave[i].ALstatuscode, ec_ALstatuscode2string(ec_slave[i].ALstatuscode));
			}
		}
		goto exit_simple;
	}

	 /* **PL**
	  * void ecx_dcsync01(ecx_contextt *context, uint16 slave, boolean act,
	  * uint32 CyclTime0, uint32 CyclTime1, int32 CyclShift)
	  * NOTE: if CyclTime1 = 0 then SYNC1 fires a the same time as SYNC0.
	  * 
	  * ec_dcsync01(1, TRUE, 1000000U, 1000000U / 2, 0);
	  */
	for(i = 1; i <= ec_slavecount ; i++) {
		if (!(strcmp(ec_slave[i].name, "PIC32 EtherCAT MSP16BIT"))) {
			printf("Enabling SYNC0/1 for slave: %s.\n", ec_slave[i].name);
			ec_dcsync01(i, TRUE, 1000000U, 0, 0);
		} else if (!(strcmp(ec_slave[i].name, "EasyCAT 16+16 rev 1"))) {
			printf("Enabling SYNC0/1 for slave: %s.\n", ec_slave[i].name);
			ec_dcsync01(i, TRUE, 1000000U, 1000000U / 2, 0);
		}
	}

	printf("All slaves reached safe operational state.\n");
	 /* read indevidual slave state and store in ec_slave[] */
	ec_readstate();
	for(cnt = 1; cnt <= ec_slavecount ; cnt++) {
		printf("Slave:%d Name:%s Output size:%3dbits Input size:%3dbits State=0x%2.2x delay:%d.%d\n",
		cnt, ec_slave[cnt].name, ec_slave[cnt].Obits, ec_slave[cnt].Ibits,
		ec_slave[cnt].state, (int)ec_slave[cnt].pdelay, ec_slave[cnt].hasdc);
	}

	oloop = ec_slave[0].Obytes;
	if ((oloop == 0) && (ec_slave[0].Obits > 0))
		oloop = 1;
#if 0
	if (oloop > 8)
		oloop = 8;
#endif
	iloop = ec_slave[0].Ibytes;
	if ((iloop == 0) && (ec_slave[0].Ibits > 0))
		iloop = 1;
#if 0
	if (iloop > 17)
		iloop = 17;
#endif
	printf("segments : %d : %d %d %d %d\n",ec_group[0].nsegments ,ec_group[0].IOsegment[0],ec_group[0].IOsegment[1],ec_group[0].IOsegment[2],ec_group[0].IOsegment[3]);

	printf("Request operational state for all slaves\n");
	expectedWKC = (ec_group[0].outputsWKC * 2) + ec_group[0].inputsWKC;
	printf("Calculated workcounter ((%d x 2) + %d) = %d\n", ec_group[0].outputsWKC, ec_group[0].inputsWKC, expectedWKC);

	/* send one valid process data to make outputs in slaves happy*/
	ec_slave[0].state = EC_STATE_OPERATIONAL;
	ec_send_processdata();
	ec_receive_processdata(EC_TIMEOUTRET);
	/* request OP state for all slaves */
	i = ec_writestate(0);
	printf("ec_writestate return %d\n", i);
	ec_statecheck(0, EC_STATE_OPERATIONAL,  EC_TIMEOUTSTATE * 2);
	chk = 40;
	/* wait for all slaves to reach OP state */
	do
	{
		ec_send_processdata();
		ec_receive_processdata(EC_TIMEOUTRET);
		ec_statecheck(0, EC_STATE_OPERATIONAL, 50000);
		osal_usleep(10000);
	}
	while (chk-- && (ec_slave[0].state != EC_STATE_OPERATIONAL));

	if (ec_slave[0].state != EC_STATE_OPERATIONAL) {
		printf("Not all slaves reached operational state (master state: 0x%2.2x).\n", ec_slave[0].state);
		ec_readstate();
		for(i = 1; i <= ec_slavecount ; i++) {
			if(ec_slave[i].state != EC_STATE_OPERATIONAL)
				printf("Slave %d State=0x%2.2x StatusCode=0x%4.4x : %s\n",
				       i, ec_slave[i].state, ec_slave[i].ALstatuscode,
				       ec_ALstatuscode2string(ec_slave[i].ALstatuscode));
		}
		goto exit_simple;
	}

	printf("Operational state reached for all slaves.\n");
#ifdef PL_DC_TEST
	dorun = 1;
#endif
	inOP = TRUE;
	 /* acyclic loop 100000000UL x 5000ms  */
	for(loop = 1;  loop <= 100000000UL; loop++) {
#ifndef PL_DC_TEST
		ec_send_processdata();
		wkc = ec_receive_processdata(EC_TIMEOUTRET);
                if (wkc < expectedWKC) {
			osal_usleep(5000);
			continue;
		}
		printf("Processdata cycle %llu, WKC %d , O:", loop, wkc);
#else
		printf("%llu: Processdata cycle %5d , Wck %3d, DCtime %12lld, dt %12lld, O:",
// 		printf("%llu: Processdata cycle %5d , Wck %3d, DCtime %12ld, dt %12ld, O:",
		      loop, dorun, wkc , ec_DCtime, gl_delta);
#endif
#ifdef PL_PRINT_OP_REQUIRED
		if (slave1_microchip_lan9252)
			printf(" %2.2x", *(ec_slave[0].outputs + MODULE_INDEX_LED_D24));
#else
		for(j = 0 ; j < oloop; j++)
			printf(" %2.2x", *(ec_slave[0].outputs + j));
#endif
		printf(" I:");

		if (slave1_microchip_lan9252) {
			for(j = 0 ; j < 5; j++)
				printf(" %2.2x", *(ec_slave[0].inputs + j));
			printf(" %2.2x\n", *(ec_slave[0].inputs + 16));
		} else {
			for(j = 0 ; j < iloop; j++)
				printf(" %2.2x", *(ec_slave[0].inputs + j));
			printf("\n");
		}

#ifndef PL_DC_TEST
		printf(" T:%"PRId64"\n",ec_DCtime);
		needlf = TRUE;
#endif
		fflush(stdout);
		osal_usleep(20000);
	}
#ifdef PL_DC_TEST
	dorun = 0;
#endif
	inOP = FALSE;

	ec_dcsync01(1, FALSE, 1000000U, 1000000U/2, 0);
	printf("\nRequest init state for all slaves\n");
	ec_slave[0].state = EC_STATE_INIT;
	/* request INIT state for all slaves */
	ec_writestate(0);

exit_simple:
        printf("End simple test, close socket\n");
        /* stop SOEM, close socket */
        ec_close();
}

#ifdef PL_DC_TEST
/* add ns to timespec */
static inline void add_timespec(struct timespec *ts, int64 addtime)
{
	int64 sec, nsec;

	nsec = addtime % NSEC_PER_SEC;
	sec = (addtime - nsec) / NSEC_PER_SEC;
	ts->tv_sec += sec;
	ts->tv_nsec += nsec;
	if (ts->tv_nsec < NSEC_PER_SEC)
		return;

	nsec = ts->tv_nsec % NSEC_PER_SEC;
	ts->tv_sec += (ts->tv_nsec - nsec) / NSEC_PER_SEC;
	ts->tv_nsec = nsec;
}

/* PI calculation to get linux time synced to DC time */
static inline void ec_sync(int64 reftime, int64 cycletime , int64 *offsettime)
{
	static int64 integral = 0;
	int64 delta;

	/* set linux sync point 50us later than DC sync, just as example */
	delta = (reftime - 50000) % cycletime;
	if (delta > (cycletime / 2))
		delta= delta - cycletime;
	
	if (delta > 0)
		integral++;
	if (delta < 0)
		integral--;

	*offsettime = -(delta / 100) - (integral / 20);
	gl_delta = delta;
}

static inline void slave_digital_output(void)
{
	int i;

	for(i = 1; i <= ec_slavecount ; i++) {
		if (!(strcmp(ec_slave[i].name, "PIC32 EtherCAT MSP16BIT"))) {
			static boolean led24 = FALSE;
			boolean flip = FALSE;

			if (!(dorun % 30))
				flip = TRUE;

			if (flip)
				led24 = !led24;
			set_output_bit(1, MODULE_INDEX_LED_D24, led24);
		} else if (!(strcmp(ec_slave[i].name, "EasyCAT 16+16 rev 1"))) {
			/* TODO: DO SOMETHING */
		}
	}


}

/* RT EtherCAT thread */
static OSAL_THREAD_FUNC_RT ecatthread(void *ptr)
{
	struct timespec   ts, tleft;
	int ht;
	int64 cycletime;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	ht = (ts.tv_nsec / 1000000) + 1; /* round to nearest ms */
	ts.tv_nsec = ht * 1000000;
	cycletime = *(int*)ptr * 1000; /* cycletime in ns */
	toff = 0;
	dorun = 0;
	ec_send_processdata();

	while(1) {
		/* calculate next cycle start */
		add_timespec(&ts, cycletime + toff);
		/* wait to cycle start */
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, &tleft);
		if (dorun > 0) {
			wkc = ec_receive_processdata(EC_TIMEOUTRET);

			dorun++;
			slave_digital_output();

			if (ec_slave[0].hasdc)
				/* calulate toff to get linux time and DC synced */
				ec_sync(ec_DCtime, cycletime, &toff);

			ec_send_processdata();
		}
	}
}
#endif

static OSAL_THREAD_FUNC ecatcheck(void *ptr)
{
	int slave;
	(void)ptr;                  /* Not used */

	while(1) {
		if (!(inOP && ((wkc < expectedWKC) ||
			ec_group[currentgroup].docheckstate))) {
			osal_usleep(10000);
			continue;
		}

		/* one ore more slaves are not responding */
		ec_group[currentgroup].docheckstate = FALSE;
		if (needlf) {
			needlf = FALSE;
			printf("\n");
		}

		ec_readstate();
		for (slave = 1; slave <= ec_slavecount; slave++) {
			if ((ec_slave[slave].group == currentgroup) && (ec_slave[slave].state != EC_STATE_OPERATIONAL)) {

				ec_group[currentgroup].docheckstate = TRUE;

				if (ec_slave[slave].state == (EC_STATE_SAFE_OP + EC_STATE_ERROR)) {
					printf("ERROR : slave %d is in SAFE_OP + ERROR, attempting ack.\n", slave);
					ec_slave[slave].state = (EC_STATE_SAFE_OP + EC_STATE_ACK);
					ec_writestate(slave);
				} else if(ec_slave[slave].state == EC_STATE_SAFE_OP) {
					printf("WARNING : slave %d is in SAFE_OP, change to OPERATIONAL.\n", slave);
					ec_slave[slave].state = EC_STATE_OPERATIONAL;
					ec_writestate(slave);
				} else if(ec_slave[slave].state > EC_STATE_NONE) {
					if (ec_reconfig_slave(slave, EC_TIMEOUTMON)) {
						ec_slave[slave].islost = FALSE;
						printf("MESSAGE : slave %d reconfigured\n",slave);
					}
				} else if(!ec_slave[slave].islost) {
					/* re-check state */
					ec_statecheck(slave, EC_STATE_OPERATIONAL, EC_TIMEOUTRET);
					if (ec_slave[slave].state == EC_STATE_NONE) {
						ec_slave[slave].islost = TRUE;
						printf("ERROR : slave %d lost\n",slave);
					}
				}
			} else
				ec_group[currentgroup].docheckstate = TRUE;

			if (ec_slave[slave].islost) {
				if(ec_slave[slave].state == EC_STATE_NONE) {
					if (ec_recover_slave(slave, EC_TIMEOUTMON)) {
						ec_slave[slave].islost = FALSE;
						printf("MESSAGE : slave %d recovered\n",slave);
					}
				} else {
					ec_slave[slave].islost = FALSE;
					printf("MESSAGE : slave %d found\n",slave);
				}
			}
		}

		if (!ec_group[currentgroup].docheckstate)
			printf("OK : all slaves resumed OPERATIONAL.\n");

		osal_usleep(10000);
    }
}

static void handler(int sig)
{
	printf("\nhandler %d: Request init state for all slaves\n", sig);
	ec_dcsync01(1, FALSE, 1000000U, 1000000U/2, 0);
	ec_slave[0].state = EC_STATE_INIT;
	/* request INIT state for all slaves */
	ec_writestate(0);

        /* stop SOEM, close socket */
        ec_close();
	exit(0);
}

static void inst(const int sig)
{
	sighandler_t rslt = signal(sig, handler);
	if (rslt == SIG_ERR) {
		fprintf(stderr, "Could not set up signal handler: ");
		perror(0);
		exit(1);
	}
}

#define stack64k (64 * 1024)

int main(int argc, char *argv[])
{
	int ctime = 1000;

	printf("SOEM (Simple Open EtherCAT Master)\nSimple test\n");

	if (argc > 1) {
		inst(SIGHUP);
		inst(SIGINT);
		inst(SIGTERM);

		if (argc > 2)
			slave1_microchip_lan9252 = FALSE;
#ifdef PL_DC_TEST
		/* create RT thread */
		osal_thread_create_rt(&thread1, stack64k * 2, &ecatthread, (void*) &ctime);
#endif
		/* create thread to handle slave error handling in OP */
		osal_thread_create(&thread1, stack64k * 2, &ecatcheck, (void*) &ctime);
		/* start cyclic part */
		simpletest(argv[1]);
	} else {
		printf("Usage: simple_test ifname1\nifname = eth0 for example\n");
	}

	printf("End program\n");

	return 0;
}
