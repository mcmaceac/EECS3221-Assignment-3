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
    int			new; /* alarm is new = 1, 0 otherwise */
    int			modified; /* alarm modfied = 1, 0 otherwise */
    int			linked; /* alarm is in list = 1, 0 otherwise */
} alarm_t;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rw_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
alarm_t *head, *tail;
int read_count;

/*
 * Searches for a type A alarm in the alarm list
 * If alarm is found, returns 1
 * Returns 0 otherwise
 */
int searchAlarmA(alarm_t *alarm)
{
    alarm_t *next;
    int flag;

    flag = 0;
    next = head->link;
    while (next != tail)
    {
	//printf("Searching Alarm Number %d...\n", alarm->alarmNum);
 	if (next->alarmNum == alarm->alarmNum
		&& next->type == 1)
	{
	    flag = 1;
	    break;
	}
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
    alarm_t *next;
    int flag;

    flag = 0;
    next = head->link;
    while (next != tail)
    {
 	if (next->alarmNum == alarm->alarmNum
		&& next->type == 0)
	{
	    flag = 1;
	    break;
	}
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
    alarm_t *next;

    next = head->link;
    while (next != tail)
    {
        if (next->alarmNum == alarm->alarmNum
		&& next->type == 1)
	{
	    strcpy(next->message, alarm->message);
    	    next->time = alarm->time;
    	    next->seconds = alarm->seconds;
	    next->modified = 1;
	    break;
	}
        next = next->link;
    }
}

void printAlarmList()
{
     alarm_t *next;

     next = head;
     while (next != NULL) 
     {
	printf("%d, ", next->alarmNum);
	next = next->link;
     }
     printf("\n");
}

/*
 * Insert alarm entry on list, in order.
 */
void alarm_insert (alarm_t *alarm)
{
    int status;
    alarm_t *previous, *next;
    int flagA, flagB;

    /*
     * LOCKING PROTOCOL:
     * 
     * This routine requires that the caller have locked the
     * alarm_mutex!
     */
    //printf("Enetered alarm_insert\n");
    flagA = 0;
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
		//printAlarmList();
	    }
	    if (!flagA)
	    {
		printf("First Alarm Request With Message Number (%d) " 	
			   "Received at %d: %s\n",alarm->alarmNum, time(NULL),
			   alarm->message);
		previous = head;
	    	next = head->link;
		while (next != tail) {
		    	if (next->alarmNum >= alarm->alarmNum) {
			    alarm->link = next;
			    previous->link = alarm;
			    alarm->linked = 1;
			    break;
			}
			previous = next;
			next = next->link;
	    	}
		
		if (next == tail) {
			alarm->link = next;
			previous->link = alarm;
			alarm->linked = 1;
		}
		//printAlarmList();
		/*status = pthread_cond_signal (&alarm_cond);
        	if (status != 0)
            		err_abort (status, "Signal cond");*/
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
			previous = head;
	    		next = head->link;
			while (next != tail) {
		    		if (next->alarmNum >= alarm->alarmNum) {
			    	alarm->link = next;
			    	previous->link = alarm;
				alarm->linked = 1;
			   	 break;
				}
				previous = next;
				next = next->link;
			}
			//printAlarmList();
			/*status = pthread_cond_signal (&alarm_cond);
        		if (status != 0)
            			err_abort (status, "Signal cond");*/
	    	}		
	   }
     }
    
#ifdef DEBUG
    printf ("[list: ");
    for (next = alarm_list; next != NULL; next = next->link)
        printf ("%d(%d)[\"%s\"] ", next->time,
            next->time - time (NULL), next->message);
    printf ("]\n");
#endif
    /*
     * Wake the alarm thread if it is not busy (that is, if
     * current_alarm is 0, signifying that it's waiting for
     * work), or if the new alarm comes before the one on
     * which the alarm thread is waiting.
     */
    /*if (current_alarm == 0 || alarm->alarmNum < current_alarm) {
        current_alarm = alarm->alarmNum;
        status = pthread_cond_signal (&alarm_cond);
        if (status != 0)
            err_abort (status, "Signal cond");
    }*/
}

void *periodic_display_thread (void *arg)
{
    alarm_t *alarm;
    int status;
    int sleeptime;
    int flag;
	
    flag = 0;
    alarm = arg;
    while(1)
    {
	status = pthread_mutex_lock (&mutex);
	if (status != 0)
	    err_abort (status, "Lock mutex");
	read_count++;
	if (read_count == 1)
	{
	    status = pthread_mutex_lock (&rw_mutex);
                if (status != 0)
                    err_abort (status, "Lock mutex");
	}
	status = pthread_mutex_unlock (&mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");

	sleeptime = alarm->seconds;
	if(alarm->linked == 0)
	{
	    printf("Display thread exiting at %d: %s\n", time(NULL), alarm->message);
	    status = pthread_mutex_lock (&mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");
	    read_count--;
	    if (read_count == 0)
	    {
	        status = pthread_mutex_unlock (&rw_mutex);
                if (status != 0)
                    err_abort (status, "Unlock mutex");
	    }
	    status = pthread_mutex_unlock (&mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");
	    return NULL;
	}
	if (alarm->modified == 0)
	    printf("Alarm With Message Number (%d) Displayed at %d: %s\n",
		    alarm->alarmNum, time(NULL), alarm->message);
	if (alarm->modified == 1 && flag)
	{
	    printf("Replacement Alarm With Message Number (%d) Displayed at "
		   "%d: %s\n", alarm->alarmNum, time(NULL), alarm->message);
	}
	if (alarm->modified == 1 && !flag)
	{
	    printf("Alarm With Message Number (%d) Replaced at %d: %s\n",
		    alarm->alarmNum, time(NULL), alarm->message);
	    flag = 1;
	}

	status = pthread_mutex_lock (&mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");
	read_count--;
	if (read_count == 0)
	{
	    status = pthread_mutex_unlock (&rw_mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");
	}
	status = pthread_mutex_unlock (&mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");

	sleep(sleeptime);
	//sleeptime = alarm->seconds;
	/*status = pthread_mutex_unlock (&alarm_mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");*/

    }
}

/*
 * The alarm thread's start routine.
 */
void *alarm_thread (void *arg)
{
    alarm_t *alarm, *next, *previous;;
    struct timespec cond_time;
    time_t now;
    int status, alarmToDelete;
    pthread_t thread;

    /*
     * Loop forever, processing commands. The alarm thread will
     * be disintegrated when the process exits. Lock the mutex
     * at the start -- it will be unlocked during condition
     * waits, so the main thread can insert alarms.
     */
    /*status = pthread_mutex_lock (&alarm_mutex);
    if (status != 0)
        err_abort (status, "Lock mutex");*/
    while (1) {
	status = pthread_mutex_lock (&mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");
	read_count++;
	if (read_count == 1)
	{
        /*
         * If the alarm list is empty, wait until an alarm is
         * added. Setting current_alarm to 0 informs the insert
         * routine that the thread is not busy.
         */
        //current_alarm = 0;
	    status = pthread_mutex_lock (&rw_mutex);
                if (status != 0)
                    err_abort (status, "Lock mutex");
	}
	/*status = pthread_cond_wait (&alarm_cond, &rw_mutex);
        if (status != 0)
            err_abort (status, "Wait on cond");*/
	status = pthread_mutex_unlock (&mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");

	alarm = NULL;
    	next = head->link;
	while (next != tail)
	{
	    if(next->new == 1)
	    {
		next->new = 0;
		alarm = next;
		break;
	    }
	    next = next->link;
	}
	if (alarm != NULL && alarm->type == 1)
	{
	    printf("Alarm Request With Message Number(%d) Proccessed at %d: %s\n",
		alarm->alarmNum, time(NULL), alarm->message);
	    status = pthread_create (
                &thread, NULL, periodic_display_thread, alarm);
    	    if (status != 0)
                err_abort (status, "Create alarm thread");
	}

	status = pthread_mutex_lock (&mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");
	read_count--;
	if (read_count == 0)
	{
	    status = pthread_mutex_unlock (&rw_mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");
	}
	status = pthread_mutex_unlock (&mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");

	if (alarm != NULL &&  alarm->type == 0)
	{
	    status = pthread_mutex_lock (&rw_mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");
	    next = head->link;
	    previous = head;
	    while (next != tail)
	    {
		//printf("Searching for type B alarm\n");
		if(next == alarm)
	        {
		    //printf("Found Type B Alarm to Delete\n");
	       	    alarmToDelete = next->alarmNum;
		    previous->link = next->link;
		    next->linked = 0;
		    break;
	        }
	    	previous = next;
	    	next = next->link;
	    }
	    //printAlarmList();
    	    next = head->link;
	    previous = head;
	    while (next != tail)
	    {
	        if(next->alarmNum == alarmToDelete)
	        {
		    next->linked = 0;
		    previous->link = next->link;
		    break;
	        }
		previous = next;
	        next = next->link;
	     }
	     //printAlarmList();
	     printf("Alarm Request With Message Number(%d) Proccessed at %d: %s\n",
		alarm->alarmNum, time(NULL), alarm->message);
	     status = pthread_mutex_unlock (&rw_mutex);
             if (status != 0)
                err_abort (status, "Lock mutex");
	}
        //alarm = alarm_list;
        //alarm_list = alarm->link;
        //now = time (NULL);
        /*expired = 0;
        if (alarm->time > now) {
#ifdef DEBUG
            printf ("[waiting: %d(%d)\"%s\"]\n", alarm->time,
                alarm->time - time (NULL), alarm->message);
#endif
            cond_time.tv_sec = alarm->time;
            cond_time.tv_nsec = 0;
            current_alarm = alarm->alarmNum;
            while (current_alarm == alarm->alarmNum) {
                status = pthread_cond_timedwait (
                    &alarm_cond, &alarm_mutex, &cond_time);
                if (status == ETIMEDOUT) {
                    expired = 1;
                    break;
                }
                if (status != 0)
                    err_abort (status, "Cond timedwait");
            }
            if (!expired)
                alarm_insert (alarm);
        } else
            expired = 1;
        if (expired) {
            printf ("(%d) %s\n", alarm->seconds, alarm->message);
            free (alarm);
        }*/
    }
}

int main (int argc, char *argv[])
{
    int status;
    char line[128];
    alarm_t *alarm;
    pthread_t thread;
    int flag;

    tail = (alarm_t*)malloc(sizeof(alarm_t));
    head = (alarm_t*)malloc(sizeof(alarm_t));
    tail->link = NULL;
    tail->alarmNum = 9999;
    head->link = tail;
    head->alarmNum = -1;
    //printAlarmList();
    read_count = 0;
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
        else alarm->type = 1;
	//printf("Alarm created\n");
        if (flag) 
        {
            status = pthread_mutex_lock (&rw_mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");
            alarm->time = time (NULL) + alarm->seconds;
	    alarm->new = 1;
	    alarm->modified = 0;
            /*
             * Insert the new alarm into the list of alarms,
             * sorted by expiration time.
             */
	    //printf("Adding alarm\n");
            alarm_insert (alarm);
            status = pthread_mutex_unlock (&rw_mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");
        }
    }
}
