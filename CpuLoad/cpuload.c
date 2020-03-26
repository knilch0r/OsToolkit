/* cpuload.c - a "background task" that measures CPU load
 *
*/
#include "cpuload.h"

/* calc_rolling() - calculate the interval average and rolling average
 *
 * This function is called whenever an interval gets filled up.
 * Actions performed:
 * 	- Adjust the rolling sum downwards by the interval that's about to be evicted from the array.
 *  - Calculate the average for the interval and store it, overwriting the average from the evicted interval.
 *	- Add the new average to the rolling sum.
 *	- Increment the rolling index modulo n_intervals
 *	- Set the busy time to zero.
 *	- Calculate a new window average and update the peak hold if necessary.
*/
static void calc_rolling(cpuload_t *cl)
{
	cl->rolling_sum -= cl->rolling[cl->rolling_i];
	cl->rolling[cl->rolling_i] = (u32_t)((cl->t_busy * CL_SCALE) / cl->t_interval);
	cl->rolling_sum += cl->rolling[cl->rolling_i];
	cl->rolling_i++;
	if ( cl->rolling_i >= CL_N_INTERVALS )
		cl->rolling_i = 0;
	cl->t_busy = 0;

	cl->average = cl->rolling_sum / CL_N_INTERVALS;
	if ( cl->peak < cl->average )
		cl->peak = cl->average;

	cl_Callout(cl);
}

/* cl_LogBusy() - logs some busy time
*/
static void cl_LogBusy(cpuload_t *cl, u64_t t)
{
	CL_DEBUG(printf("cl_LogBusy(%u)\n", t & 0xffffffff))
	u64_t interval_left = cl->t_interval - cl->t_used;

	while ( t >= interval_left )
	{
		cl->t_used += interval_left;
		cl->t_busy += interval_left;
		calc_rolling(cl);
		t -= interval_left;
		interval_left = cl->t_interval;
		cl->t_used = 0;
		cl->t_busy = 0;
	}

	cl->t_used += t;
	cl->t_busy += t;
}

/* cl_LogIdle() - logs some idle time
 *
 * Similar to cl_LogBusy(), but the after the first cal_rolling(), the busy time stays at zero.
*/
static void cl_LogIdle(cpuload_t *cl, u64_t t)
{
	CL_DEBUG(printf("cl_LogIdle(%u)\n", t & 0xffffffff))
	u64_t interval_left = cl->t_interval - cl->t_used;

	while ( t >= interval_left )
	{
		cl->t_used += interval_left;
		calc_rolling(cl);
		t -= interval_left;
		interval_left = cl->t_interval;
		cl->t_used = 0;
		cl->t_busy = 0;
	}

	cl->t_used += t;
}

/* cl_Init() - initialize the cpuload_t structure
 *
*/
static void cl_Init(cpuload_t *cl)
{
	cl->t_window	= (u64_t)CL_INTERVAL * (u64_t)CL_N_INTERVALS;	/* Length of a window, in ticks. */
	cl->t_interval	= (u64_t)CL_INTERVAL;							/* Length of an interval, in ticks */
	cl->t_threshold	= (u64_t)CL_THRESHOLD;							/* Boundary value for decision: busy or idle */

	cl->t_used = 0;			/* Amount of current interval accounted for. */
	cl->t_busy = 0;			/* Amount of current interval that was busy */
	cl->rolling_sum = 0;	/* Sum of the rolling array */
	cl->rolling_i = 0;		/* Next position in the rolling array */
	cl->average = CL_SCALE;	/* Most recent average. = rolling_sum/CL_N_INTERVAL */
	cl->peak = 0;			/* Highest average seen */

	for ( int i=0; i<CL_N_INTERVALS; i++ )
	{
		cl->rolling[i] = 0;	/* Percent busy in each interval */
	}
}

/* cl_IdleLoop() - idle-loop function that calculates CPU load
 *
 * This function runs continuously at the lowest priority
 *
 * CPU load is defined as the percentage of time that is spent *not* executing this function
 * To achieve this, we use two times:
 *		t1 = start of a potentially busy interval
 *		t2 = start of an idle interval. The calculation time is counted as idle.
 * t2-t1 is the length of the potentially busy interval. If the interval is short, it is counted as idle
 * t1-t2 is the length of the computation. This is always counted as idle.
 *
*/
void cl_IdleLoop(void)
{
	cpuload_t cl;
	u64_t t1, t2;	/* Time markers */
	u64_t te;		/* Elapsed time */

	cl_Init(&cl);

	cl_Disable();
	t2 = cl_ReadTime();

	for (;;)
	{
		t1 = cl_ReadTime();
		te = t1 - t2;				/* Time spent calculating. This is counted as "idle" */
		cl_Enable();
		cl_Barrier();
		cl_Disable();
		t2 = cl_ReadTime();

		cl_LogIdle(&cl, te);		/* Log calculation time as idle time */

		te = t2 - t1;				/* Time spent in application */

		if ( te > cl.t_threshold )	/* Log measured time as idle or busy, depending on length */
			cl_LogBusy(&cl, te);
		else
			cl_LogIdle(&cl, te);
	}
}