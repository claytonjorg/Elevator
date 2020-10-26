# Elevator

### Authors
Clayton Jorgensen, Tristan Garcia, & John Lajoie

### 1: Intro
Implementation of a scheduling algorithm for a hotel elevator. The elevator tracks
the number of passengers and the total weight. Elevator load consists of four types of people:
adults, children, room service, and bellhops:
- An adult counts as 1 passenger unit and 1 weight unit
- A child counts as 1 passenger unit and ½ weight unit
- Room service counts as 2 passenger units and 2 weight units
- A bellhop counts as 2 passengers unit and 4 weight unit

Passengers will appear on a floor of their choosing and always know where they wish to go. Assumes 
most of the time when a passenger is on a floor other than the first, they will
choose to go to the first floor (for optimization purposes). Passengers board the elevator in
FIFO order. If a passenger can fit, the elevator must accept them unless the elevator is moving
in the opposite direction from where they wish to go. Once they board the elevator, they may
only get off when the elevator arrives at the destination. Passengers will wait on floors to be
serviced indefinitely.

### 2: Kernel Module with an Elevator
Elevator supports having a maximum load of 15 weight units and 10 passenger units (neither can be exceeded at any
point). The elevator will wait for 2.0 seconds when moving between floors, and it will wait
for 1.0 seconds while loading/unloading passengers. The building has floor 1 being the
minimum floor number and floor 10 being the maximum floor number. New passengers can
arrive at any time and each floor supports an arbitrary number of them.

### 3: Add System Calls
These system calls will be used by a user-space application to control your elevator and create
passengers. Assign the system calls to the following numbers:
- 335 for start_elevator()
- 336 for issue_request()
- 337 for stop_elevator()

###### int start_elevator(void)
Description: Activates the elevator for service. From that point onward, the elevator exists and
will begin to service requests. This system call will return 1 if the elevator is already active, 0 for
a successful elevator start, and -ERRORNUM if it could not initialize (e.g. -ENOMEM if it couldn’t 
allocate memory). Initialize an elevator as follows:
- State: IDLE
- Current floor: 1
- Current load: 0 passenger units, 0 weight units

###### int issue_request(int passenger_type, int start_floor, int destination_floor)
Description: Creates a passenger of type passenger_type at start_floor that wishes
to go to destination_floor. This function returns 1 if the request is not valid (one of the
variables is out of range), and 0 otherwise. A passenger type can be translated to an int as
follows:
- Adult = 1
- Child = 2
- Room service = 3
- Bellhop = 4

###### int stop_elevator(void)
Description: Deactivates the elevator. At this point, this elevator will process no more new
requests (that is, passengers waiting on floors). However, before an elevator completely stops,
it must offload all of its current passengers. Only after the elevator is empty may it be
deactivated (state = OFFLINE). This function returns 1 if the elevator is already in the process
of deactivating, and 0 otherwise.

### 4: /Proc
Create a proc entry named /proc/elevator. 
The elevator's movement state:
- OFFLINE: when the module is installed but the elevator isn’t running (initial state)
- IDLE: elevator is stopped on a floor because there are no more passengers to service
- LOADING: elevator is stopped on a floor to load and unload passengers
- UP: elevator is moving from a lower floor to a higher floor
- DOWN: elevator is moving from a higher floor to a lower floor

Output:
- The current floor the elevator is on
- The next floor the elevator intends to service
- The elevator's current load (in terms of both passengers units and weight units)
- The load of the waiting passengers
- The total number of passengers that have been serviced

### 5: Test
producer.c
- This program will issue N random requests, specified by input.

consumer.c <--start | --stop>
- This program expects one flag and on argument:
- If the flag is --start, then the program must start the elevator
- If the flag is --stop, then the program must stop the elevator

### How To
- sudo make
- sudo make insert
- sudo make watch_proc
- sudo make issue
- sudo make stop
- sudo make remove
- sudo make clean

### Known Bugs
- Elevator does not got to IDLE state when the wait line queue is empty

### Important Notes
- producer.c & consumer.c were provided test files and claim no ownership of said files.
