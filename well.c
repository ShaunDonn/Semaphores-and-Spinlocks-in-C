#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include "uthread.h"
#include "uthread_mutex_cond.h"

#ifdef VERBOSE
#define VERBOSE_PRINT(S, ...) printf (S, ##__VA_ARGS__);
#else
#define VERBOSE_PRINT(S, ...) ;
#endif

#define MAX_OCCUPANCY      3
#define NUM_ITERATIONS     100
#define NUM_PEOPLE         20
#define FAIR_WAITING_COUNT 4

/**
 * You might find these declarations useful.
 */
enum Endianness {LITTLE = 0, BIG = 1};
const static enum Endianness oppositeEnd [] = {BIG, LITTLE};

struct Well {
  uthread_sem_t well_mutex;////
  uthread_sem_t big_endian_can_enter;////
  uthread_cond_t little_endian_can_enter;/////
  enum Endianness endian;
  int num_at_well;
  int num_entered_since_last_switch;
  int empty_the_well;
  int big_waiting;
  int little_waiting;

};

struct Well* createWell() {
  struct Well* Well = malloc (sizeof (struct Well));

  // set the endianness to "little"
  Well->endian = LITTLE;

  // create the mutex/condition variables
  Well->well_mutex = uthread_mutex_create();
  Well->big_endian_can_enter = uthread_cond_create(Well->well_mutex);
  Well->little_endian_can_enter = uthread_cond_create(Well->well_mutex);

  // initialize counters/flags to zero
  Well->num_at_well = 0;
  Well->num_entered_since_last_switch = 0;
  Well->big_waiting = 0;
  Well->little_waiting = 0;
  Well->empty_the_well  = 0;

  return Well;
}

struct Well* Well;

#define WAITING_HISTOGRAM_SIZE (NUM_ITERATIONS * NUM_PEOPLE)
int             entryTicker;                                          // incremented with each entry
int             waitingHistogram         [WAITING_HISTOGRAM_SIZE];
int             waitingHistogramOverflow;
uthread_mutex_t waitingHistogrammutex;
int             occupancyHistogram       [2] [MAX_OCCUPANCY + 1];

// Switch the endianness of the well
// ALWAYS CALLED WITHIN A LOCK
void swapEndianness() {
  Well->endian = oppositeEnd[Well->endian];
}

void leaveWell() {
  uthread_mutex_lock(Well->well_mutex);

    Well->num_at_well--;

    // if nobody is left at the well, switch endianness appropriately
    if (Well->num_at_well == 0) {

      // if the well is being emptied (due to having allowed in the fair number of endians)
      if (Well->empty_the_well == 1) {
        // swap endianness if there are opposite endians waiting
        if (Well->endian == BIG && Well->little_waiting > 0) {
          swapEndianness();
        } else if (Well->endian == LITTLE && Well->big_waiting > 0) {
          swapEndianness();
        }

        // the well has been cleared; reset flags and counters
        Well->empty_the_well  = 0;
        Well->num_entered_since_last_switch = 0;

      // if the well is not being emptied
      } else {
        // swap endianness if there are no more people of the current endianness
        if (Well->endian == BIG && Well->big_waiting == 0) {
          swapEndianness();
        } else if (Well->endian == LITTLE && Well->little_waiting == 0) {
          swapEndianness();
        }
      }
    }

    // signal threads to enter (at most 3 of appropriate endianness will need to be signalled)
    if (Well->endian == LITTLE) {
      for (int i = 0; i < 3; i++) {
        uthread_cond_signal(Well->little_endian_can_enter);
      }
    } else {
      for (int i = 0; i < 3; i++) {
        uthread_cond_signal(Well->big_endian_can_enter);
      }
    }

  uthread_mutex_unlock(Well->well_mutex);
}

// yield inside the well NUM_PEOPLE times
void loopInWell(enum Endianness g) {
  for (int i = 0; i < NUM_PEOPLE; i++) {
    assert(Well->num_at_well <= MAX_OCCUPANCY && Well->num_at_well >= 0);
    assert(Well->endian == g);
    uthread_yield();
  }
  leaveWell();
}

// record the time that a thread waited for the well
void recordWaitingTime (int waitingTime) {
  uthread_mutex_lock (waitingHistogrammutex);
  if (waitingTime < WAITING_HISTOGRAM_SIZE)
    waitingHistogram [waitingTime] ++;
  else
    waitingHistogramOverflow++;
  uthread_mutex_unlock (waitingHistogrammutex);
}


// produce true if a thread with given endianness can enter the well
// ALWAYS CALLED WITHIN A LOCK!
int can_enter(enum Endianness g) {
  if (Well->empty_the_well == 1) {

    return 0;                                   // if the well is being cleared, do not enter

  } else if (Well->endian == g) {

    return (Well->num_at_well < MAX_OCCUPANCY); // if the person has the same endianness as the well, enter if there is space

  } else {                                      // otherwise, the person has the opposite endianness

    // if the person is big endian and the well is little endian,
    if (Well->endian == LITTLE) {
      // enter only if there are no little endians waiting
      return (Well->num_at_well == 0) && (Well->little_waiting == 0);

    // if the person is little endian and the well is big endian,
    } else {
      // enter only if there are no big endians waiting
      return (Well->num_at_well == 0) && (Well->big_waiting == 0);
    }

  }
}

void enterWell (enum Endianness g) {

  // lock the mutex
  uthread_mutex_lock(Well->well_mutex);

    // save time at entry
    int initial_time = entryTicker;

    // if the thread cannot enter, wait for the appropriate signal
    // and update the appropriate waiting counters
    while (!can_enter(g)) {
      if (g == LITTLE) {
        Well->little_waiting++;
        uthread_cond_wait(Well->little_endian_can_enter);
        Well->little_waiting--;
      } else {
        Well->big_waiting++;
        uthread_cond_wait(Well->big_endian_can_enter);
        Well->big_waiting--;
      }
    }

    // save and record time
    int final_time = entryTicker;
    recordWaitingTime(final_time - initial_time);

    printf("Entering with endianness %d\n", g);
    // increment counters
    Well->num_at_well++;
    entryTicker++;
    Well->num_entered_since_last_switch++;

    // if a person with different endianness entered the well when it was empty,
    // reset the endianness and the last switch counter
    if (Well->endian != g) {
      Well->endian = g;
      Well->num_entered_since_last_switch = 0;
    }

    // if the number of people who have entered with a given endianness >= the fair waiting count,
    // start emptying the well
    if (Well->num_entered_since_last_switch >= FAIR_WAITING_COUNT) {
      Well->empty_the_well = 1;
    }

    // record the current state
    occupancyHistogram [g][Well->num_at_well]++;

  // unlock the mutex
  uthread_mutex_unlock(Well->well_mutex);

  loopInWell(g);

}


// Tries to enter the well NUM_ITERATIONS times
// After each time the person leaves the well, yields NUM_PEOPLE times outside
void * iterate(void * endv) {
  enum Endianness * end  = endv;
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    enterWell(*end);
    for (int j = 0; j < NUM_PEOPLE; j++) {
      uthread_yield();
    }
  }
  return NULL;
}

int main (int argc, char** argv) {
  uthread_init (1);

  Well = createWell();

  uthread_t pt [NUM_PEOPLE];
  waitingHistogrammutex = uthread_mutex_create ();

  entryTicker = 0;

  enum Endianness ends[NUM_PEOPLE];

  for (int i = 0; i < NUM_PEOPLE; i++) {
    int r = random();
    ends[i] = r % 2;
    uthread_t next = uthread_create(iterate, &ends[i]);
    pt[i] = next;
  }

  for (int i = 0; i < NUM_PEOPLE; i++) {
    uthread_join(pt[i], 0);
  }

  printf ("Times with 1 little endian %d\n", occupancyHistogram [LITTLE]   [1]);
  printf ("Times with 2 little endian %d\n", occupancyHistogram [LITTLE]   [2]);
  printf ("Times with 3 little endian %d\n", occupancyHistogram [LITTLE]   [3]);
  printf ("Times with 1 big endian    %d\n", occupancyHistogram [BIG] [1]);
  printf ("Times with 2 big endian    %d\n", occupancyHistogram [BIG] [2]);
  printf ("Times with 3 big endian    %d\n", occupancyHistogram [BIG] [3]);
  printf ("Waiting Histogram\n");
  for (int i=0; i<WAITING_HISTOGRAM_SIZE; i++)
    if (waitingHistogram [i])
      printf ("  Number of times people waited for %d %s to enter: %d\n", i, i==1?"person":"people", waitingHistogram [i]);
  if (waitingHistogramOverflow)
    printf ("  Number of times people waited more than %d entries: %d\n", WAITING_HISTOGRAM_SIZE, waitingHistogramOverflow);

  free(Well);

}
