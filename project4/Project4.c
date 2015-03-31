#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/neutrino.h>
#include <sys/netmgr.h>
#include <sys/syspage.h>
#include <unistd.h>
#include <stdint.h>
#include <hw/inout.h>
#include <sys/mman.h>
#include <assert.h>
#include <errno.h>

#define TELLER_NUM 3
#define OPEN_HOURS 1

// Linked list node construct for customer Queue
// Also holds individual bank processing stats
// All time fields are in simulated seconds
typedef struct Customer{
  int id; // Customer id (in order of bank entry)
  struct Customer* next; // Next customer in line
  int startWaitTime; // When customer began waiting since bank opened
  int endWaitTime; // When customer stopped waiting since bank opened
  int tellWaitTime; // Duration of time teller waited for a customer between prev customer and this customer
  int transTime; // Duration of time teller transacted with customer
  int depth; // Current depth of customer in queue
  int startingDepth; // Starting depth of customer in queue
  pthread_mutex_t wait; // Mutex for conditional wait (used in simulating transaction time)
}Customer;

// Wrapper for customer queue/line for tellers
typedef struct Customers{
  pthread_mutex_t lock; // Access mutex for queue
  Customer* q; // actual queue
  sem_t semaphore; // Semaphore indicating number of customers in queue
  pthread_t thread; // Self thread
}Customers;

// Simulated time clock
// Ticks every 1.7 ms (approximately 1 simulated second)
typedef struct ClockSim{
  int secs; // # of fake secs bank has been open
  pthread_cond_t tick; // Condition for clock tick waiting
  pthread_mutex_t lock;
  int kill; // flag to kill clock
  pthread_t thread; // Self thread
}ClockSim;

// Bank Vars Wrapper
typedef struct Bank{
  int closed; // Boolean whether bank is closed
  ClockSim clock; // Clock simulation wrapper
  pthread_t thread; // Self thread
  pthread_mutex_t lock; //variable access lock
  pthread_mutex_t wait; //mutex for conditional wait
  pthread_cond_t open; //condition for conditional wait
  Customers customers; // Wrapper around queue of customers to be served
  Customer* served; // Queue of customers already served
  pthread_t tellers[TELLER_NUM]; // Teller Threads
}Bank;

// Global Var
Bank bank;

// Params in fake seconds
unsigned int randomWait(unsigned int minSecs, unsigned int maxSecs, unsigned int* seed, Customer* cust) {
  unsigned int waitTime = minSecs + rand_r(seed) % (maxSecs - minSecs);
  unsigned int counter  = waitTime;
  pthread_mutex_lock(&cust->wait);
  while(counter > 0) {
    pthread_cond_wait(&bank.clock.tick, &cust->wait); //wait for sim clock tick
    counter--;
  }
  pthread_mutex_unlock(&cust->wait);
  return waitTime;
}

// Traverses linked list to the tail
Customer* traverse(Customer* head, int* depth) {
  if(head == NULL) return head;
  (*depth)++;
  head->depth = *depth;
  if(head->next == NULL) return head;
  return traverse(head->next, depth);
}

// Convenience method for finding tail
Customer* findTail(Customer* head) {
  int tailDepth = 0;
  return traverse(head, &tailDepth);
}

// Remove next node for queue and return it
Customer* dequeue(Customer** queue) {
  Customer* head = NULL;
  if(*queue != NULL) {
    head = *queue;
    if((*queue)->next != NULL) {
      *queue = (*queue)->next;
    } else {
      *queue = NULL;
    }
    head->next = NULL;
  }
  return head;
}

// Add node to end of queue
int enqueue(Customer* cust, Customer** queue) {
  if(cust == NULL) return 0;
  Customer* tail = findTail(*queue);
  if(tail == NULL) {
    cust->depth = 1;
    *queue = cust;
  } else {
    cust->depth = tail->depth+1;
    tail->next = cust;
  }
  return cust->depth;
}

// dequeue with mutex guarding access and line stat logic
Customer* getNextCust() {
  Customer* nextCust = NULL;
  pthread_mutex_lock(&bank.customers.lock);
  nextCust = dequeue(&bank.customers.q);
  pthread_mutex_unlock(&bank.customers.lock);
  return nextCust;
}

// enqueue with mutex guarding access and line stat logic
void addCustomer(Customer* newCust) {
  if(newCust == NULL) return;
  pthread_mutex_lock(&bank.customers.lock);
  enqueue(newCust, &bank.customers.q);
  newCust->startingDepth = newCust->depth; // Set starting depth for customer
  pthread_mutex_unlock(&bank.customers.lock);
  sem_post(&bank.customers.semaphore);
}

// Teller thread function
void* teller(void* arg) {
  unsigned int wait = 0;
  unsigned int seed = 12;
  int startWait = 0;
  int endWait = 0;
  unsigned int lowBoundWait = 30;
  unsigned int upperBoundWait = 60 * 6;

  Customer* cur = NULL;
  for(;;) {
    startWait = bank.clock.secs;
    sem_wait(&bank.customers.semaphore);
    endWait = bank.clock.secs;
    cur = getNextCust();
    if(cur == NULL) {
      break;
    }
    cur->endWaitTime = bank.clock.secs;
    wait = randomWait(lowBoundWait, upperBoundWait, &seed, cur); // Sim transaction

    // Set stats for transaction
    cur->endWaitTime = bank.clock.secs;
    cur->transTime = wait;
    cur->tellWaitTime = endWait - startWait;
    enqueue(cur, &bank.served); // Add to served queue
  }
}

// Customer generation thread function
void* customerGen(void) {
  int id = 1;
  int stopGenerating = 0;
  unsigned int seed = 42;
  unsigned int lowerBoundWait = 60;
  unsigned int upperBoundWait = 4 * 60;
  Customer* cust = NULL;
  for(;;) {
    pthread_mutex_lock(&bank.lock);
    stopGenerating = bank.closed;
    pthread_mutex_unlock(&bank.lock);
    if(stopGenerating) break;
    cust = (Customer*)malloc(sizeof(Customer));
    pthread_mutex_init(&cust->wait, NULL);
    randomWait(lowerBoundWait, upperBoundWait, &seed, cust);
    cust->id = id;
    addCustomer(cust);
    cust->startWaitTime = bank.clock.secs;
    id++;
  }
}

// Bank simulation thread function
void* bankClock(int* closingTime) {
  struct _pulse pulse;
  int pid;
  int chid;
  int pulse_id = 0;
  timer_t timer_id;
  struct sigevent event;
  struct itimerspec timer;
  struct _clockperiod clkper;
  struct sched_param param;
  int ret;

  param.sched_priority = sched_get_priority_max( SCHED_RR );
  ret = sched_setscheduler( 0, SCHED_RR, &param);
  assert ( ret != -1 ); // if returns a -1 for failure we stop with error

  clkper.nsec = 100000;
  clkper.fract = 0;
  ClockPeriod ( CLOCK_REALTIME, &clkper, NULL, 0 );

  chid = ChannelCreate( 0 );
  event.sigev_notify = SIGEV_PULSE;   // most basic message we can send -- just a pulse number
  event.sigev_coid = ConnectAttach ( ND_LOCAL_NODE, 0, chid, 0, 0 );  // Get ID that allows me to communicate on the channel
  assert ( event.sigev_coid != -1 );    // stop with error if cannot attach to channel
  event.sigev_priority = getprio(0);
  event.sigev_code = 1023;        // arbitrary number assigned to this pulse
  event.sigev_value.sival_ptr = (void*)pulse_id;
  if ( timer_create( CLOCK_REALTIME, &event, &timer_id ) == -1 )  // CLOCK_REALTIME available in all POSIX systems
  {
    perror ( "cannot create timer" );
    exit( EXIT_FAILURE );
  }
  timer.it_value.tv_sec = 0;
  timer.it_value.tv_nsec = 1700000; //roughly one fake second
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_nsec = 1700000;
  /* Start the timer. */
  if ( timer_settime( timer_id, 0, &timer, NULL ) == -1 )
  {
    perror("Cannot start timer.\n");
    exit( EXIT_FAILURE );
  }
  for(;;) {
    if(bank.clock.kill) break;
    pid = MsgReceivePulse (chid, &pulse, sizeof( pulse ), NULL);
    bank.clock.secs++;
    pthread_cond_broadcast(&bank.clock.tick);
    if(bank.clock.secs > *closingTime) pthread_cond_signal(&bank.open);
  }
}

// Bank simulation thread function
void* runBank() {
  pthread_cond_init(&bank.open, NULL);
  pthread_mutex_init(&bank.wait, NULL);

  printf("Bank opening\n");
  pthread_mutex_lock(&bank.wait);
  pthread_cond_wait(&bank.open, &bank.wait);
  pthread_mutex_unlock(&bank.wait);
  pthread_mutex_lock(&bank.lock);
  bank.closed = 1;
  pthread_mutex_unlock(&bank.lock);
  printf("Bank Closing\n");
  int i = 0;
  for(i = 0; i < TELLER_NUM; i++) {
    sem_post(&bank.customers.semaphore);
  }
}

void openBank() {
  // Init bank vars
  bank.clock.kill = 0;
  bank.closed = 0;
  bank.clock.secs = 0;
  int openTimeSecs = OPEN_HOURS * 60 * 60;
  pthread_cond_init(&bank.clock.tick, NULL);
  pthread_mutex_init(&bank.customers.lock, NULL);
  pthread_mutex_init(&bank.lock, NULL);
  pthread_mutex_init(&bank.clock.lock, NULL);
  sem_init(&bank.customers.semaphore, 0, 0);

  // Set lower priority for threads
  pthread_attr_t threadAttributes ;
  int policy;
  struct sched_param parameters ;
  pthread_attr_init(&threadAttributes) ;    // initialize thread attributes structure -- must do before any other activity on this struct
  pthread_getschedparam(pthread_self(), &policy, &parameters) ; // get this main thread's scheduler parameters
  parameters.sched_priority--;                  // lower the priority
  pthread_attr_setschedparam(&threadAttributes, &parameters) ;  // set up the pthread_attr struct with the updated priority

  pthread_create(&bank.clock.thread, NULL, &bankClock, &openTimeSecs); // Start sim clock
  pthread_create(&bank.thread, &threadAttributes, &runBank, NULL); // Start bank
  int i;
  for(i = 0; i < TELLER_NUM; i++) {
    pthread_create(&bank.tellers[i], &threadAttributes, &teller, i+1); // Start tellers
  }
  pthread_create(&bank.customers.thread, &threadAttributes, &customerGen, NULL); // Start customer generation
}

// Wait till teller threads have finished and kill clock thread
void closeBank() {
  int i;
  for(i = 0; i < TELLER_NUM; i++) {
    pthread_join(bank.tellers[i], NULL);
  }
  bank.clock.kill = 1;
}

void stats() {
  int maxDepth = 0;
  int maxTransTime = 0;
  int numServed = 0;
  int tmpCustWait = 0;
  int maxCustWait = 0;
  int totalCustWait = 0;
  int totalTransTime = 0;
  int maxTelWait = 0;
  int totalTelWait = 0;
  Customer* cust = bank.served;
  Customer* tmp = NULL;
  for(;;) {
    if(cust == NULL) break;
    if(cust->startingDepth > maxDepth) maxDepth = cust->startingDepth;
    if(maxTransTime < cust->transTime) maxTransTime = cust->transTime;
    if(maxTelWait < cust->tellWaitTime) maxTelWait = cust->tellWaitTime;
    tmpCustWait = cust->endWaitTime - cust->startWaitTime;
    totalCustWait += tmpCustWait;
    if(maxCustWait < tmpCustWait) maxCustWait = tmpCustWait;
    totalTransTime += cust->transTime;
    numServed++;
    totalTelWait += cust->tellWaitTime;
    if(cust->next == NULL) break;
    tmp = cust;
    cust = cust->next;
    free(tmp);
    tmp = NULL;
  }
  printf("Total customers served today: %d\n", numServed);
  printf("The maximum queue depth was %d\n", maxDepth);
  printf("The maximum teller transaction time was %d\n", maxTransTime);
  printf("The average transaction time was: %d\n", totalTransTime/numServed);
  printf("The maximum teller wait time was %d\n", maxTelWait);
  printf("The average teller wait time was %d\n", totalTelWait/numServed);
  printf("The maximum customer wait time was %d\n", maxCustWait);
  printf("The average customer wait time was %d\n", totalCustWait/numServed);
}

int main(int argc, char *argv[]) {
  openBank();
  closeBank();
  stats();
  printf("DONE\n");
  return EXIT_SUCCESS;
}
