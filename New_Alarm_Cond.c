/*
 * alarm_cond.c
 *
 * This is an enhancement to the alarm_mutex.c program, which
 * used only a mutex to synchronize access to the shared alarm
 * list. This version adds a condition variable. The alarm
 * thread waits on this condition variable, with a timeout that
 * corresponds to the earliest timer request. If the main thread
 * enters an earlier timeout, it signals the condition variable
 * so that the alarm thread will wake up and process the earlier
 * timeout first, requeueing the later request.
 */
#include <pthread.h>
#include <time.h>
#include "errors.h"

/*
 * The "alarm" structure now contains the time_t (time since the
 * Epoch, in seconds) for each alarm, so that they can be
 * sorted. Storing the requested number of seconds would not be
 * enough, since the "alarm thread" cannot tell how long it has
 * been on the list.
 */
typedef struct alarm_tag {
    struct alarm_tag    *link;
    int                 seconds;
    time_t              time;   /* seconds from EPOCH */
    char                message[64];
    int			alarmNum; /* the message number */
    int			type; /*alarm type: 1 = type A, 0 = Type B*/
} alarm_t;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
alarm_t *alarm_list = NULL;
time_t current_alarm = 0;

/*
 * Searches for a type A alarm in the alarm list
 * If alarm is found, returns 1
 * Returns 0 otherwise
 */
int searchAlarmA(alarm_t *alarm)
{
    alarm_t **last, *next;
    int flag;

    flag = 0;
    last = &alarm_list;
    next = *last;
    while (next != NULL)
    {
		/*printf("Searching...\n");*/
	 	if (next->alarmNum == alarm->alarmNum && next->type == 1)
		{
			flag = 1;
			break;
		}
		last = &next->link;
		next = next->link;
    }
    //printf("Done searching\n");
    return flag;
}

/*
 * Searches for a type B alarm in the alarm list
 * If alarm is found, returns 1
 * Returns 0 otherwise
 */
int searchAlarmB(alarm_t *alarm)
{
    alarm_t **last, *next;
    int flag;

    flag = 0;
    last = &alarm_list;
    next = *last;
    while (next != NULL)
    {
	 	if (next->alarmNum == alarm->alarmNum
			&& next->type == 0)
		{
			flag = 1;
			break;
		}
		last = &next->link;
		next = next->link;
    }
    return flag;
}

/*
 * Searches for a type A alarm in the alarm list
 * Replaces the alarm in the list with this alarm
 */
void replaceAlarmA(alarm_t *alarm)
{
    alarm_t **last, *next;

    last = &alarm_list;
    next = *last;
    while (next != NULL)
    {
        if (next->alarmNum == alarm->alarmNum
		&& next->type == 1)
		{
	    	strcpy(next->message, alarm->message);
    	    next->time = alarm->time;
    	    next->seconds = alarm->seconds;
	    	break;
		}
		last = &next->link;
        next = next->link;
    }
}

/*
 * Insert alarm entry on list, in order.
 */
void alarm_insert (alarm_t *alarm)
{
    int status;
    alarm_t **last, *next;
    int flagA, flagB;

    /*
     * LOCKING PROTOCOL:
     * 
     * This routine requires that the caller have locked the
     * alarm_mutex!
     */
    //printf("Enetered alarm_insert\n");
    flagA = 0;
    last = &alarm_list;
    next = *last;
    //printf("Checking alarm type\n");
    if(alarm->type == 1)
    {
	    //printf("Alarm Type A\n");
	    flagA = searchAlarmA(alarm);
	    //printf("%d\n", flagA);
	    if(flagA) 
	    {
			printf("Replacement Alarm Request With Message Number (%d) " 	
			   "Received at %d: %s\n",alarm->alarmNum, time(NULL),
			   alarm->message);
			replaceAlarmA(alarm);
	    }
        if (!flagA)
	    {
			printf("First Alarm Request With Message Number (%d) " 	
			   "Received at %d: %s\n",alarm->alarmNum, time(NULL),
			   alarm->message);
			last = &alarm_list;
	    	next = *last;
			while (next != NULL) 
			{
		    	if (next->alarmNum >= alarm->alarmNum) {
					alarm->link = next;
					*last = alarm;
					break;
				}
				last = &next->link;
				next = next->link;
	    	}
		
			if (next == NULL) {
				*last = alarm;
				alarm->link = NULL;
			}
        }
    }
    else
    {
		flagA = searchAlarmA(alarm);
		if(!flagA) printf("Error: No Alarm Request With Message Number (%d) " 	
			   "to Cancel!\n",alarm->alarmNum);
			if(flagA)
			{
				flagB=searchAlarmB(alarm);
				if(flagB) printf("Error: More Than One Request to Cancel "
					   "Alarm Request With Message Number (%d)!\n"
						   ,alarm->alarmNum);
				else
				{
					printf("Cancel Alarm Request With Message Number (%d) " 	
					   "Received at %d: %s\n",alarm->alarmNum, time(NULL),
					   alarm->message);
					last = &alarm_list;
						next = *last;
					while (next != NULL) 
					{
						if (next->alarmNum >= alarm->alarmNum) 
						{
							alarm->link = next;
							*last = alarm;
					   	 	break;
						}
						last = &next->link;
						next = next->link;
					}
				}		
			}
     }
    
#ifdef DEBUG
    printf ("[list: ");
    for (next = alarm_list; next != NULL; next = next->link)
        printf ("%d(%d)[\"%s\"] ", next->time, next->time - time (NULL), next->message);
    printf ("]\n");
#endif
    /*
     * Wake the alarm thread if it is not busy (that is, if
     * current_alarm is 0, signifying that it's waiting for
     * work), or if the new alarm comes before the one on
     * which the alarm thread is waiting.
     */
    if (current_alarm == 0 || alarm->alarmNum < current_alarm) 
	{
        current_alarm = alarm->alarmNum;
        status = pthread_cond_signal (&alarm_cond);
        if (status != 0)
            err_abort (status, "Signal cond");
    }
}

/*
 * The alarm thread's start routine.
 */
void *alarm_thread (void *arg)
{
    alarm_t *alarm;
    struct timespec cond_time;
    time_t now;
    int status, expired;

    /*
     * Loop forever, processing commands. The alarm thread will
     * be disintegrated when the process exits. Lock the mutex
     * at the start -- it will be unlocked during condition
     * waits, so the main thread can insert alarms.
     */
    status = pthread_mutex_lock (&alarm_mutex);
    if (status != 0)
        err_abort (status, "Lock mutex");
    while (1) 
	{
        /*
         * If the alarm list is empty, wait until an alarm is
         * added. Setting current_alarm to 0 informs the insert
         * routine that the thread is not busy.
         */
        current_alarm = 0;
        while (alarm_list == NULL) 
		{
            status = pthread_cond_wait (&alarm_cond, &alarm_mutex);
            if (status != 0)
                err_abort (status, "Wait on cond");
		}
        alarm = alarm_list;
        alarm_list = alarm->link; /** **/
        now = time (NULL);
        expired = 0;

        if (alarm->time > now) 
		{
#ifdef DEBUG
            printf ("[waiting: %d(%d)\"%s\"]\n", alarm->time,
                alarm->time - time (NULL), alarm->message);
#endif
            cond_time.tv_sec = alarm->time;
            cond_time.tv_nsec = 0;
            current_alarm = alarm->alarmNum;
            while (current_alarm == alarm->alarmNum) 
			{
                status = pthread_cond_timedwait (
                    &alarm_cond, &alarm_mutex, &cond_time);
                if (status == ETIMEDOUT) 
				{
                    expired = 1;
                    break;
                }
                if (status != 0)
                    err_abort (status, "Cond timedwait");
            }
            if (!expired)
                alarm_insert (alarm);
        } 
		else
            expired = 1;
        if (expired) 
		{
            printf ("(%d) %s\n", alarm->seconds, alarm->message);
            free (alarm);
        }
    }
}

int main (int argc, char *argv[])
{
    int status;
    char line[128];
    alarm_t *alarm;
    pthread_t thread;
    int flag;

    status = pthread_create (
        &thread, NULL, alarm_thread, NULL);
    if (status != 0)
        err_abort (status, "Create alarm thread");
    while (1) {
	flag = 1;
        printf ("Alarm> ");
        if (fgets (line, sizeof (line), stdin) == NULL) exit (0);
        if (strlen (line) <= 1) continue;
        alarm = (alarm_t*)malloc (sizeof (alarm_t));
        if (alarm == NULL)
            errno_abort ("Allocate alarm");

        /*
         * Parse input line into seconds (%d) and a message
         * (%64[^\n]), consisting of up to 64 characters
         * separated from the seconds by whitespace.
         */
	//printf("Input Read\n");
        if (sscanf (line, "%d Message(%d) %64[^\n]", 
            &alarm->seconds, &alarm->alarmNum, alarm->message) < 3) 
			if (sscanf (line, "Cancel: Message(%d)",
			    &alarm->alarmNum) < 1)
		    {
		        fprintf (stderr, "Bad command\n");
		       	free (alarm);
				flag = 0;
			}
			else
			{
				alarm->seconds = 0;
				strcpy(alarm->message,"Cancel command");
				alarm->type = 0;
				flag = 1;
			}
        else 
			alarm->type = 1;
	//printf("Alarm created\n");
        if (flag) 
        {
            status = pthread_mutex_lock (&alarm_mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");
            alarm->time = time (NULL) + alarm->seconds;
            /*
             * Insert the new alarm into the list of alarms,
             * sorted by expiration time.
             */
	    //printf("Adding alarm\n");
            alarm_insert (alarm);
            status = pthread_mutex_unlock (&alarm_mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");
        }
    }
}
