/*
 * New_Alarm_Cond.c
 *
 * This program creates recurring alarms that trigger priodically
 * based on the user input.
 * Existing alarms can be modified by changing the alarm message or
 * the alarm wait time.
 * Alarms can be canceled, after which they will never display again.
 */
#include <pthread.h>
#include <time.h>
#include "errors.h"

/*
 * The "alarm" structure contains different variables that help the
 * program easily idetify different states in which the alarm can be.
 */
typedef struct alarm_tag {
    struct alarm_tag *link;      /* pointer to the next alarm in the alarm list
	 			  * that is stored as a linked list 
				  */ 
    int              seconds;    /* the nuber of seconds that the alarm will
				  * wait before displaying the alarm message
				  * periodically
				  */
    time_t           time;       /* seconds from EPOCH */
    char             message[64];/* the alarm message */
    int	      	     alarmNum;   /* the alarm message number */
    int	      	     type;       /* alarm type: 1 = type A, 0 = Type B*/
    int	      	     new;        /* alarm is new = 1, 0 otherwise */
    int	      	     modified;   /* alarm modfied = 1, 0 otherwise */
    int	      	     linked;     /* alarm is in list = 1, 0 otherwise */
} alarm_t;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;   /* semaphore for safe
						      * access of read_count
						      */
pthread_mutex_t rw_mutex = PTHREAD_MUTEX_INITIALIZER;/* semaphore for safe
						      * acess of alarm list
						      */
alarm_t *head, *tail;		 /* dummy variables used to point to the head
				  * and tail of the alarm list
				  */
int read_count;			 /* stores the number of threads that are
				  * currently reading the alarm list
				  */

/* HELPER METHOD
 * 
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
 	if (next->alarmNum == alarm->alarmNum
		&& next->type == 1)
	{
	    flag = 1;
	    break;
	}
        next = next->link;
    }
    return flag;
}

/* HELPER METHOD
 *
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

/* HELPER METHOD
 *
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

/* DEBUGGING METHOD
 *
 * Prints the alarm list in one line in the terminal
 */
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

/* Part of the MAIN thread.
 * 
 * Insert alarm to alarm list, in non-decreasing order based on alarm number.
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
    * rw_mutex!
    */

    flagA = 0;
    if(alarm->type == 1)
    {
	flagA = searchAlarmA(alarm);
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
	    previous = head;
	    next = head->link;
	    while (next != tail) 
	    {
                if (next->alarmNum >= alarm->alarmNum) 
		{
		    alarm->link = next;
	            previous->link = alarm;
	            alarm->linked = 1;
	            break;
	 	}
		previous = next;
		next = next->link;
	    }
		
	    if (next == tail) 
	    {
		alarm->link = next;
		previous->link = alarm;
		alarm->linked = 1;
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
	    if(flagB) 
		printf("Error: More Than One Request to Cancel "
		       "Alarm Request With Message Number (%d)!\n"
		       ,alarm->alarmNum);
	    else
	    {
		printf("Cancel Alarm Request With Message Number (%d) " 	
		       "Received at %d: %s\n",alarm->alarmNum, time(NULL),
		       alarm->message);
		previous = head;
	    	next = head->link;
		while (next != tail) 
 		{
		    if (next->alarmNum >= alarm->alarmNum) 
		    {
			alarm->link = next;
			previous->link = alarm;
			alarm->linked = 1;
			break;
		    }
		    previous = next;
		    next = next->link;
		}
	    }		
	}
    }
}

/*
 * Periodic display thread
 * Periodically displays the alarm message, Frequency is specified in the alarm
 * by user input.
 * Each alarm is handled by its own thread.
 * Terminates when the alarm is cancled by the user.
 */
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
	/* Reader locking setup */
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
	/* Reader locking setup complete */	

	/* Reading is performed */
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
	/* Reading is done */	

	/* Reader unlocking setup*/
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
	/* Reader unlocking setup complete*/

	sleep(sleeptime); /* 
			   * Threads sleeps for the time period of specified in
			   * the alarm by the user input.
			   */
    }
}

/*
 * The alarm thread.
 * Searches for new alarms in the alarm list.
 * Upon finding a new type A alarm, creates a new periodic display thread for
 * that alarm.
 * Upon finding a new type B alarm, removes both type A and B alarms with the 
 * corresponding alarm number.
 */
void *alarm_thread (void *arg)
{
    alarm_t *alarm, *next, *previous;;
    struct timespec cond_time;
    time_t now;
    int status, alarmToDelete;
    pthread_t thread;

    while (1) 
    {
	/* Reader locking setup */
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
	/* Reader locking setup complete */	

	/* Reading is performed */
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
	/* Reading is done */	

	/* Reader unlocking setup*/
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
	/* Reader unlocking setup complete*/

	if (alarm != NULL &&  alarm->type == 0)
	{
	    /* Writer locking rw_mutex */
	    status = pthread_mutex_lock (&rw_mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");
	    next = head->link;
	    previous = head;
	    while (next != tail)
	    {
		if(next == alarm)
	        {
	       	    alarmToDelete = next->alarmNum;
		    previous->link = next->link;
		    next->linked = 0;
		    break;
	        }
	    	previous = next;
	    	next = next->link;
	    }
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
	     printf("Alarm Request With Message Number(%d) Proccessed at %d: %s\n",
		alarm->alarmNum, time(NULL), alarm->message);
	     status = pthread_mutex_unlock (&rw_mutex);
             if (status != 0)
                err_abort (status, "Lock mutex");
	     /* Writer unlocking rw_mutex */
	}
    }
}

/*
 * The main thread.
 * Reads and parses the user input correctly. 
 * In case of incorrect input prints clear error messages to stdout.
 * Inserts both alarm type A and B into the alarm list.
 */
int main (int argc, char *argv[])
{
    int status;
    char line[128];
    alarm_t *alarm;
    pthread_t thread;
    int flag; /* 
	       * flag = 1 if input was parsed correctly as either type A or B
  	       * alarm. flag = 0 otherwise.
	       */

    /* 
     * Initializing the dummy variables of the alarm list.
     * The "head" is the front of the alarm list, and the "tail" is the end of 
     * the alarm list.
     * Initial alarm list configuration is Head -> Tail -> NULL
     */
    tail = (alarm_t*)malloc(sizeof(alarm_t));
    head = (alarm_t*)malloc(sizeof(alarm_t));
    tail->link = NULL;
    tail->alarmNum = 9999; /* Used for debugging purposes only */
    head->link = tail;
    head->alarmNum = -1;   /* Used for debugging purposes only */
    read_count = 0;	   /* Initializing reader count to 0 */
    status = pthread_create (&thread, NULL, alarm_thread, NULL);
    if (status != 0)
        err_abort (status, "Create alarm thread");
    while (1) 
    {
	flag = 1;
        printf ("Alarm> ");
        if (fgets (line, sizeof (line), stdin) == NULL) exit (0);
        if (strlen (line) <= 1) continue;
        alarm = (alarm_t*)malloc (sizeof (alarm_t));
        if (alarm == NULL)
            errno_abort ("Allocate alarm");
        /*
         * Parse input line into seconds (%d), a message number (%d) and a 
	 * message (%64[^\n]), consisting of up to 64 characters
         * separated from the seconds by whitespace.
         */
        if (sscanf (line, "%d Message(%d) %64[^\n]", 
            &alarm->seconds, &alarm->alarmNum, alarm->message) < 3) 
	    /*
             * Parse input line into a message number (%d)
             */
	    if (sscanf (line, "Cancel: Message(%d)",
	        &alarm->alarmNum) < 1)
            {
	    /* Error in case the input is wrong */
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
        if (flag) 
        {
            status = pthread_mutex_lock (&rw_mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");
            alarm->time = time (NULL) + alarm->seconds;
	    alarm->new = 1;
	    alarm->modified = 0;
            /*
             * Insert the new alarm into the alarm list,
             * sorted by alarm number.
             */
            alarm_insert (alarm);
            status = pthread_mutex_unlock (&rw_mutex);
            if (status != 0)
                err_abort (status, "Unlock mutex");
        }
    }
}
