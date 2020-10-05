#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");

#define ENTRY_NAME "elevator"
#define PERMS 0644
#define PARENT NULL

#define kmallocFlags (__GFP_RECLAIM | __GFP_IO | __GFP_FS)

#define OFFLINE 0
#define IDLE 1
#define LOADING 2
#define UP 3
#define DOWN 4

#define ADULT 1
#define CHILD 2
#define ROOM_SERVICE 3
#define BELLHOP 4

#define NUM_FLOORS 10

#define MAX_WEIGHT 30
#define MAX_PASSENGERS 10

struct passengers 
{
  struct list_head list;
  int type;
  int startFloor;
  int destFloor;
};

struct thread_parameter
{
        int id;
        int cnt;
        struct task_struct *kthread;
};

struct thread_parameter thread1;
struct list_head passengerQueue[NUM_FLOORS];
struct list_head elevatorList;

struct mutex passengerQueueMutex;
struct mutex elevatorListMutex;

static struct file_operations fops;

static char *message;
static int read_p;

int beginStopping;
int state;
int nextState;
int currFloor;
int nextFloor;
int currPassengers;
int currWeight;
int totalPassengers;
int stopElevator;
int passengersServiced;
int numWaiting;
int passengerTypeCount[] = {0, 1, 1, 2, 2};		//Type # (defined above) corresponds to Passenger #
int passengerTypeWeight[] = {0, 2, 1, 4, 8};	//Type # (defined above) corresponds to Weight #
int totalPassengersByFloor[NUM_FLOORS];
int totalPassengersServedByFloor[NUM_FLOORS];
int z;				//Used to iterate through loops


/* ======================= Elevator Code Helpers ======================= */

void addPassenger(int type, int start, int end) 
{
	struct passengers *newPassenger;
	newPassenger = kmalloc(sizeof(struct passengers), kmallocFlags);
	newPassenger->type = type;
	newPassenger->startFloor = start;
	newPassenger->destFloor = end;
	mutex_lock_interruptible(&passengerQueueMutex);
	list_add_tail(&newPassenger->list, &passengerQueue[start - 1]);
	totalPassengersByFloor[start - 1] += passengerTypeCount[type];
	mutex_unlock(&passengerQueueMutex); 
}

int elevator_loading(void)
{
	struct passengers *Passenger;
	struct list_head *pos;

	mutex_lock_interruptible(&passengerQueueMutex);
	list_for_each(pos, &passengerQueue[currFloor - 1])
	{
		Passenger = list_entry(pos, struct passengers, list);
		if(currWeight + passengerTypeWeight[Passenger->type] <= MAX_WEIGHT
		&& currPassengers + passengerTypeCount[Passenger->type] <= MAX_PASSENGERS
		&& ((Passenger->destFloor > currFloor && nextState == UP) 
			|| (Passenger->destFloor < currFloor && nextState == DOWN)))
		{
			mutex_unlock(&passengerQueueMutex);
			return 1;
		}
	}
	mutex_unlock(&passengerQueueMutex);
	return 0;
}

int elevator_unloading(void)
{
	struct passengers *Passenger;
	struct list_head *pos;

	mutex_lock_interruptible(&elevatorListMutex);
	list_for_each(pos, &elevatorList) 
	{
		Passenger = list_entry(pos, struct passengers, list);
		if(Passenger->destFloor == currFloor)
		{
			mutex_unlock(&elevatorListMutex);
			return 1;
		}
	}
	mutex_unlock(&elevatorListMutex);
	return 0;
}

void elevator_load_passengers(int floor)
{
	struct passengers *Passenger;
	struct list_head *pos, *q;

	mutex_lock_interruptible(&passengerQueueMutex);
	list_for_each_safe(pos, q, &passengerQueue[floor - 1]) 
	{
		Passenger = list_entry(pos, struct passengers, list);
		if (Passenger->startFloor == floor
			&& currWeight + passengerTypeWeight[Passenger->type] <= MAX_WEIGHT
			&& currPassengers + passengerTypeCount[Passenger->type] <= MAX_PASSENGERS)
		{
			struct passengers *newPassenger;
			newPassenger = kmalloc(sizeof(struct passengers), kmallocFlags);
			newPassenger->type = Passenger->type;
			newPassenger->startFloor = Passenger->startFloor;
			newPassenger->destFloor = Passenger->destFloor;
			mutex_lock_interruptible(&elevatorListMutex);
			list_add_tail(&newPassenger->list, &elevatorList);
			currWeight += passengerTypeWeight[newPassenger->type];
			currPassengers += passengerTypeCount[newPassenger->type];
			totalPassengersByFloor[floor - 1] -= passengerTypeCount[newPassenger->type];
			list_del(pos);
			kfree(Passenger);
			mutex_unlock(&elevatorListMutex);
			mutex_unlock(&passengerQueueMutex);
		}
	}
	mutex_unlock(&passengerQueueMutex);
}

void elevator_unload_passengers(void)
{
	struct passengers *Passenger;
	struct list_head *pos, *q;

	mutex_lock_interruptible(&elevatorListMutex);
	list_for_each_safe(pos, q, &elevatorList) 
	{
		Passenger = list_entry(pos, struct passengers, list);
		if (Passenger->destFloor == currFloor) 
		{
			currWeight -= passengerTypeWeight[Passenger->type];
			currPassengers -= passengerTypeCount[Passenger->type];
			totalPassengers += passengerTypeCount[Passenger->type];
			totalPassengersServedByFloor[Passenger->startFloor - 1] += passengerTypeCount[Passenger->type];
			list_del(pos);
			kfree(Passenger);
		}
	}
	mutex_unlock(&elevatorListMutex);
}

char* statusString(int state)
{
	static char str[32];
	if(state == OFFLINE)
		sprintf(str, "OFFLINE");
	else if(state == IDLE)
		sprintf(str, "IDLE");
	else if(state == LOADING)
		sprintf(str, "LOADING");
	else if(state == UP)
		sprintf(str, "UP");
	else if(state == DOWN)
		sprintf(str, "DOWN");
	else
		sprintf(str, "ERROR");

	return str;
}

/* ====================== Elevator Code Control ======================= */

int elevatorMain(void *data)
{
	struct thread_parameter *parm = data;
	for(z = 0; z < NUM_FLOORS; ++z)
	{
		totalPassengersByFloor[z] = 0;
		totalPassengersServedByFloor[z] = 0;
	}
	totalPassengers = 0;
	while (!kthread_should_stop()) 
	{
		// Main call area ... i.e. when the elevator should load and unload; move up or down etc...
		parm->cnt++;
		
		if(state == IDLE)
		{
			nextState = UP;
			if(elevator_loading() && !beginStopping)
				state = LOADING;
			else
			{
				state = UP;
				nextFloor = currFloor + 1;
			}
		}
		else if(state == UP)
		{
			if(currFloor != nextFloor)
			{
				ssleep(2);
				currFloor = nextFloor;
				printk("Arrived at Floor %d\n", currFloor);
			}
			if(currFloor == 10)
			{
				state = DOWN;
				nextState = DOWN;
			}
			if((elevator_loading() && !beginStopping) || elevator_unloading())
				state = LOADING;
			else if(currFloor == 10)
			{
				nextFloor = currFloor - 1;
			}
			else
			{
				nextFloor = currFloor + 1;
			}
		}
		else if(state == DOWN)
		{
			if(currFloor != nextFloor)
			{
				ssleep(2);
				currFloor = nextFloor;
				printk("Arrived at Floor %d\n", currFloor);
			}
			if(currFloor == 1)
			{
				state = UP;
				nextState = UP;
			}
			if(beginStopping && currFloor == 1)
			{
				state = OFFLINE;
				beginStopping = 0;
				nextState = UP;	
			}
			else if((elevator_loading() && !beginStopping) || elevator_unloading())
				state = LOADING;
			else if(currFloor == 1)
			{
				nextFloor = currFloor + 1;
			}
			else
			{
				nextFloor = currFloor - 1;
			}

		}
		else if(state == LOADING)
		{
			ssleep(1);
			elevator_unload_passengers();
			while(elevator_loading() && !beginStopping)
			{
				elevator_load_passengers(currFloor);
			}
			state = nextState;
			if (state == DOWN) 
			{
                		if (currFloor == 1)
				{
                 			nextState = UP;
                  			state = UP;
                  			nextFloor = currFloor + 1;
               			}
				else
				{
                  			nextFloor = currFloor - 1;
                		}
              		}
			else
			{
                		if (currFloor == 10)
				{
                  			nextState = DOWN;
                  			state = DOWN;
                  			nextFloor = currFloor - 1;
				}
				else
                  			nextFloor = currFloor + 1;
			}
		}
		else if(state == OFFLINE)
		{
			ssleep(1); //needed so thread doesn't eat reasources
		}
	}
	return 0;
}

void thread_init_parameter(struct thread_parameter *parm)
{
	static int id = 1;
	parm->id = id++;
	parm->cnt = 0;
	parm->kthread = kthread_run(elevatorMain, parm, "thread elevator %d", parm->id);
}


/* ====================== File Operations ============================ */

int elevator_proc_open(struct inode *sp_inode, struct file *sp_file) 
{
	char* buf;
	char* messageBuff = kcalloc(1000, sizeof(char), kmallocFlags);
	char* temp = kmalloc(sizeof(char) * 100, kmallocFlags);
	printk(KERN_INFO "proc called open\n");
	read_p = 1;
	
	buf = kmalloc(sizeof(char) * 1000, kmallocFlags);
	if (buf == NULL)
	{
		printk(KERN_WARNING "elevator_proc_open");
		return -ENOMEM;
	}

	message = kmalloc(sizeof(char) * 2048, kmallocFlags);
	if (message == NULL) 
	{
		printk(KERN_WARNING "elevator_proc_open");
		return -ENOMEM;
	}
	
	if(currWeight % 2 == 0)
	{
		sprintf(message, "Status: %s\nCurrent Floor: %d\nNext Floor: %d\nCurrent Passengers: %d\nCurrent Weight: %d\nTotal Passengers Served: %d\n",
		statusString(state), currFloor, nextFloor, currPassengers, currWeight/2, totalPassengers);
	}
	else
	{
		sprintf(message, "Status: %s\nCurrent Floor: %d\nNext Floor: %d\nCurrent Passengers: %d\nCurrent Weight: %d.5\nTotal Passengers Served: %d\n",
		statusString(state), currFloor, nextFloor, currPassengers, currWeight/2, totalPassengers);
	}
	z = 0;
	while(z < NUM_FLOORS)
	{
		strcat(messageBuff, "Floor ");
		sprintf(temp, "%d", z+1);
		strcat(messageBuff, temp);
		strcat(messageBuff, ": ");
		sprintf(temp, "%d", totalPassengersServedByFloor[z]);
		strcat(messageBuff, temp);
		strcat(messageBuff, " passengers served, ");
		sprintf(temp, "%d", totalPassengersByFloor[z]);
		strcat(messageBuff, temp);
		strcat(messageBuff, " passengers waiting\n");
		++z;
	}
	strcat(message, messageBuff);

	return 0;
}

ssize_t elevator_proc_read(struct file *sp_file, char __user *buf, size_t size, loff_t *offset) 
{
	int len = strlen(message);

	read_p = !read_p;
	if (read_p) 
		return 0;

	printk(KERN_INFO "proc called read\n");
	copy_to_user(buf, message, len);

	return len;
}

int elevator_proc_release(struct inode *sp_inode, struct file *sp_file) 
{
	printk(KERN_INFO "proc called release\n");
	kfree(message);
	return 0;
}

/* ====================== SyscallModules =============================  */

extern long (*STUB_start_elevator)(void);
long start_elevator(void) 
{
	if(state == OFFLINE)
	{
		state = IDLE;
		nextState = UP;
		currFloor = 1;
		nextFloor = 2;
		currWeight = 0;
		currPassengers = 0;
		beginStopping = 0;
		z = 0;
		while(z < NUM_FLOORS)
		{
			totalPassengersByFloor[z] = 0;
			totalPassengersServedByFloor[z] = 0;
			++z;
		}
		return 0;
	}
	else if(state == UP || state == DOWN || state == LOADING || state == IDLE)
	{
		return 1;
	}
	else
	{
		return -ERRORNUM;
	}
}

extern long (*STUB_stop_elevator)(void);
long stop_elevator(void) 
{
	if (beginStopping == 1) 
	{
		return 1;
	}
	else if(currFloor == 1 && currPassengers == 0)
	{
		state = OFFLINE;
		return 0;
	}
	else
	{
		beginStopping = 1;
		return 0;
	}
}

extern long (*STUB_issue_request)(int,int,int);
long issue_request(int passenger_type,int start_floor,int destination_floor) 
{
	printk("New request: %d, %d => %d\n", passenger_type, start_floor, destination_floor);
	if(start_floor == destination_floor)
	{
		totalPassengersByFloor[start_floor - 1] += passengerTypeCount[passenger_type];
	}
	else
	{
		addPassenger(passenger_type,start_floor,destination_floor);
	}
	return 0;
}

static int elevator_init(void) 
{
	int i = 0;
	printk(KERN_NOTICE "/proc/%s create\n", ENTRY_NAME);
	fops.open = elevator_proc_open;
	fops.read = elevator_proc_read;
	fops.release = elevator_proc_release;

	state = OFFLINE;
	nextState = UP;
	currFloor = 1;
	currPassengers = 0;
	currWeight = 0;
	stopElevator = 0;
	beginStopping = 0;

	while (i < NUM_FLOORS) 
	{
		INIT_LIST_HEAD(&passengerQueue[i]);
		i++;
	}
	INIT_LIST_HEAD(&elevatorList);

	STUB_start_elevator = &(start_elevator);
	STUB_issue_request = &(issue_request); 
	STUB_stop_elevator = &(stop_elevator);

	mutex_init(&passengerQueueMutex);
	mutex_init(&elevatorListMutex);

	thread_init_parameter(&thread1);
	if (IS_ERR(thread1.kthread))
	{
		printk(KERN_WARNING "error spawning thread");
		remove_proc_entry(ENTRY_NAME, NULL);
		return PTR_ERR(thread1.kthread);
	}

	if(!proc_create(ENTRY_NAME, PERMS, NULL, &fops)) 
	{
		printk("Error: proc_create\n");
		remove_proc_entry(ENTRY_NAME, NULL);
		return -ENOMEM;
	}
	return 0; 
}

module_init(elevator_init);

static void elevator_exit(void) 
{
	kthread_stop(thread1.kthread);
	remove_proc_entry(ENTRY_NAME, NULL);

	STUB_start_elevator = NULL;
	STUB_stop_elevator = NULL;
	STUB_issue_request = NULL;
	
	printk(KERN_NOTICE "Removing /proc/%s.\n", ENTRY_NAME);
}

module_exit(elevator_exit);
