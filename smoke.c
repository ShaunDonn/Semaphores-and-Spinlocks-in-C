#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include "uthread.h"
#include "uthread_mutex_cond.h"

#define NUM_ITERATIONS 500

#ifdef VERBOSE
#define VERBOSE_PRINT(S, ...) printf (S, ##__VA_ARGS__);
#else
#define VERBOSE_PRINT(S, ...) ;
#endif

struct Agent {
  uthread_mutex_t mutex;
  uthread_cond_t  match;
  uthread_cond_t  paper;
  uthread_cond_t  tobacco;
  uthread_cond_t  smoke;
  uthread_cond_t  PcanSmoke;
  uthread_cond_t  TcanSmoke;
  uthread_cond_t  McanSmoke;
};

struct Agent* createAgent() {
  struct Agent* agent = malloc (sizeof (struct Agent));
  agent->mutex   = uthread_mutex_create();
  agent->paper   = uthread_cond_create (agent->mutex);
  agent->match   = uthread_cond_create (agent->mutex);
  agent->tobacco = uthread_cond_create (agent->mutex);
  agent->smoke   = uthread_cond_create (agent->mutex);
  agent->PcanSmoke = uthread_cond_create (agent->mutex);
  agent->TcanSmoke = uthread_cond_create (agent->mutex);
  agent->McanSmoke = uthread_cond_create (agent->mutex);
  return agent;
}

uthread_t tobaccoCnt, paperCnt, matchCnt, tobaccoSmk, paperSmk, matchSmk;



//
// TODO
// You will probably need to add some procedures and struct etc.
//

/**
 * You might find these declarations helpful.
 *   Note that Resource enum had values 1, 2 and 4 so you can combine resources;
 *   e.g., having a MATCH and PAPER is the value MATCH | PAPER == 1 | 2 == 3
 */
enum Resource            {    MATCH = 1, PAPER = 2,   TOBACCO = 4};
char* resource_name [] = {"", "match",   "paper", "", "tobacco"};

int signal_count [5];  // # of times resource signalled
int smoke_count  [5];  // # of times smoker with resource smoked
int itVal = 0; //global variable that represents the sum of resources that have been made available by agent during the current iteration, based on enum Resource values above

/**
 * This is the agent procedure.  It is complete and you shouldn't change it in
 * any material way.  You can re-write it if you like, but be sure that all it does
 * is choose 2 random reasources, signal their condition variables, and then wait
 * wait for a smoker to smoke.
 */
void* agent (void* av) {
  struct Agent* a = av;
  static const int choices[]         = {MATCH|PAPER, MATCH|TOBACCO, PAPER|TOBACCO};
  static const int matching_smoker[] = {TOBACCO,     PAPER,         MATCH};
  
  uthread_mutex_lock (a->mutex);
    for (int i = 0; i < NUM_ITERATIONS; i++) {
      int r = random() % 3;
      signal_count [matching_smoker [r]] ++;
      int c = choices [r];
      if (c & MATCH) {
        VERBOSE_PRINT ("match available\n");
        uthread_cond_signal (a->match);
      }
      if (c & PAPER) {
        VERBOSE_PRINT ("paper available\n");
        uthread_cond_signal (a->paper);
      }
      if (c & TOBACCO) {
        VERBOSE_PRINT ("tobacco available\n");
        uthread_cond_signal (a->tobacco);
      }
      VERBOSE_PRINT ("agent is waiting for smoker to smoke\n");
      uthread_cond_wait (a->smoke);
    }
  uthread_mutex_unlock (a->mutex);
  return NULL;
}


void* matchSmoke(void* v){ //method for smoker who already has matches (requires paper & tobacco)
  struct Agent* a = v;
  uthread_mutex_lock(a->mutex);
  while(1){
    uthread_cond_wait(a->McanSmoke); //wait for paper & tobacco to both be available
    smoke_count[MATCH]++;
    itVal = 0; //reset global variable
    uthread_cond_signal(a->smoke); //signal that smoker has smoked
  } 
  uthread_mutex_unlock(a->mutex);
  return NULL;
}


void* paperSmoke(void* v){ //method for smoker who already has paper (requires matches & tobacco)
  struct Agent* a = v;
  uthread_mutex_lock(a->mutex);
  while(1){
    uthread_cond_wait(a->PcanSmoke); //wait for matches & tobacco to both be available
    smoke_count[PAPER]++;
    itVal = 0; //reset global variable
    uthread_cond_signal(a->smoke); //signal that smoker has smoked
  }
  uthread_mutex_unlock(a->mutex);
  return NULL;
}

 
void* tobaccoSmoke(void* v){ //method for smoker who already has tobacco (requires paper & matches)
  struct Agent* a = v;
  uthread_mutex_lock(a->mutex);
  while(1){
    uthread_cond_wait(a->TcanSmoke); //wait for paper & matches to both be available
    smoke_count[TOBACCO]++;
    itVal = 0; //reset global variable
    uthread_cond_signal(a->smoke); //signal that smoker has smoked
  }
  uthread_mutex_unlock(a->mutex);
  return NULL;
}


void* countMatches(void* v){ //method that deals with the case of agent making matches available
  struct Agent* a = v;
  uthread_mutex_lock(a->mutex);
  while(1){
    
    if(itVal != 3 || itVal != 5){
      uthread_cond_wait(a->match); //wait for agent to make matches available
      itVal += MATCH; //increase global variable by value of MATCH (1)
    }
    if(itVal == 3){
      uthread_cond_signal(a->TcanSmoke); //signal that smoker with tobacco can smoke (matches & paper have been made available)
    }
    if(itVal == 5){
      uthread_cond_signal(a->PcanSmoke); //signal that smoker with paper can smoke (matches & tobacco have been made available)
    }
  }
  uthread_mutex_unlock(a->mutex);
  return NULL;
}


void* countPaper(void* v){ //method that deals with the case of agent making paper available
  struct Agent* a = v;
  uthread_mutex_lock(a->mutex);
  while(1){
    if(itVal != 3 || itVal != 6){
      uthread_cond_wait(a->paper); //wait for agent to make paper available
      itVal += PAPER; // increase global variable by value of PAPER (2)
    }
    if(itVal == 3){
      uthread_cond_signal(a->TcanSmoke); //signal that smoker with tobacco can smoke (matches & paper have been made available)
    }
    if(itVal == 6){
      uthread_cond_signal(a->McanSmoke); //signal that smoker with matches can smoke (tobacco & paper have been made available)
    }
  }
  uthread_mutex_unlock(a->mutex);
  return NULL;
}


void* countTobacco(void* v){ //method that deals with the case of agent making tobacco available
  struct Agent* a = v;
  uthread_mutex_lock(a->mutex);
  while(1){
    if(itVal != 5 || itVal != 6){ //if neither combination of tobacco & matches or tobacco & paper is available
      uthread_cond_wait(a->tobacco); //wait for agent to make tobacco available
      itVal += TOBACCO; // increase global variable by value of TOBACCO (4)
    }
    if(itVal == 5){
      uthread_cond_signal(a->PcanSmoke); //signal that smoker with paper can smoke (matches & tobacco have been made available)
    }
    if(itVal == 6){
      uthread_cond_signal(a->McanSmoke); //signal that smoker with matches can smoke (paper & tobacco have been made available)
    }
  }
  uthread_mutex_unlock(a->mutex);
  return NULL;
}


int main (int argc, char** argv) {
  uthread_init (7);
  struct Agent*  a = createAgent();

  tobaccoSmk = uthread_create(tobaccoSmoke, a);
  matchSmk = uthread_create(matchSmoke, a);
  paperSmk = uthread_create(paperSmoke, a);
  tobaccoCnt = uthread_create(countTobacco, a);
  matchCnt = uthread_create(countMatches, a);
  paperCnt = uthread_create(countPaper, a);

  for(int i = 0; i < 6; i++){
    uthread_yield(); //yield prevents wait/signal race condition by forcing agent to wait for all other threads to wait before it begins executing
  }
  uthread_join (uthread_create (agent, a), 0);
  assert (signal_count [MATCH]   == smoke_count [MATCH]);
  assert (signal_count [PAPER]   == smoke_count [PAPER]);
  assert (signal_count [TOBACCO] == smoke_count [TOBACCO]);
  assert (smoke_count [MATCH] + smoke_count [PAPER] + smoke_count [TOBACCO] == NUM_ITERATIONS);
  printf ("Smoke counts: %d matches, %d paper, %d tobacco\n",
          smoke_count [MATCH], smoke_count [PAPER], smoke_count [TOBACCO]);
}