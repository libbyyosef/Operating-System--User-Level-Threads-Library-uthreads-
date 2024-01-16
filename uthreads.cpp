#include <iostream>
#include <vector>
#include <setjmp.h>
#include <signal.h>
#include <algorithm>
#include <sys/time.h>
#include <list>

#include "uthreads.h"

#define MAX_THREAD_NUM 100 /* maximal number of threads */
#define STACK_SIZE 4096 /* stack size per thread (in bytes) */
#define JB_SP 6
#define JB_PC 7
#define SECOND 1000000
#define SYSTEM_ERROR_MSG "system error: "
#define SIGACTION_ERROR "sigaction error.\n"
#define SETITIMER_ERROR "setitimer error.\n"
#define THREAD_LIBRARY_ERROR "thread library error: "
#define ENTRY_POINT_ERROR "entry point is nullptr.\n"
#define NUM_THREADS_ERROR "cannot add new thread, the number of threads \
exist is the maximum number.\n"
#define THREAD_DOESNT_EXIST_ERROR "thread doesn't exist.\n"
#define BLOCK_MAIN_THREAD_ERROR "can't block the main thread.\n"
#define MAIN_THREAD_SLEEP_ERROR "main thread can't be put to sleep.\n"
#define THREAD_QUANTUMS_ERROR "thread tid is invalid or not exist - \
therefore can't return thread's quantums.\n"
#define NEGATIVE_QUANTUM_ERROR "quantum values must be non negative\n"

using namespace std;

typedef enum {
    RUNNING, BLOCKED, READY, NOTHING
} STATE;

struct thread {
    int tid = -1;
    char stack[STACK_SIZE];
    int quantums;
    thread_entry_point entryPoint;
    int sleep_time;
    STATE state = NOTHING;
};

struct itimerval timer;
int create_new_thread (thread_entry_point entry_point, int new_thread_id);
int set_up_thread (thread_entry_point entry_point, thread *new_thread);
void thread_finish_sleep (int tid);
int handle_sleep (int num_quantums);
sigjmp_buf env[MAX_THREAD_NUM];
thread all_threads[MAX_THREAD_NUM];
vector<int> ready;
vector<int> blocked;
vector<int> sleeping;
thread *running_thread;
int available_id[MAX_THREAD_NUM] = {};    //0 if available, 1 otherwise
int total_quantum = 0;
thread *main_thread;
vector<thread *> pointers_to_free;

/* code for 64 bit Intel arch */

typedef unsigned long address_t;
sigset_t set;

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address (address_t addr)
{
  address_t ret;
  asm volatile("xor    %%fs:0x30,%0\n"
               "rol    $0x11,%0\n"
      : "=g" (ret)
      : "0" (addr));
  return ret;
}

void mask_signal (int sig)
{
  sigemptyset (&set);
  sigaddset (&set, SIGVTALRM);
  sigprocmask (sig, &set, NULL);
}

void erase_thread_from_sleep (int tid)
{
  auto to_erase_sleep = find (sleeping.begin (), sleeping.end (), tid);//TODO
  if (to_erase_sleep != sleeping.end ())
    {
      sleeping.erase (to_erase_sleep);
    }
}

void update_sleeping_threads ()
{
  vector<int> threads_finish_sleep;
  for (auto &trd_num: sleeping)
    {
      all_threads[trd_num].sleep_time--;
      if (all_threads[trd_num].sleep_time == 0)
        {
          threads_finish_sleep.push_back (trd_num);
        }
    }
  for (auto &id: threads_finish_sleep)
    {
      erase_thread_from_sleep (id);
      auto is_blocked = find (blocked.begin (), blocked.end (), id);
      if (is_blocked == blocked.end ())
        {
          ready.push_back (id);
          all_threads[id].state = READY;
        }
    }
}

void jump_to_thread ()
{
  mask_signal (SIG_BLOCK);
  running_thread->quantums++;
  total_quantum++;
  update_sleeping_threads ();
  mask_signal (SIG_UNBLOCK);
  siglongjmp (env[running_thread->tid], 1);
}

void switch_threads (STATE state)
{
  int pre_id = running_thread->tid;
  running_thread = &all_threads[*ready.begin ()];
  all_threads[running_thread->tid].state = RUNNING;
  all_threads[pre_id].state = state;
  if (state == NOTHING)
    {
      available_id[pre_id] = 0;
    }
  if (state == READY)
    {
      ready.push_back (pre_id);
    }
  ready.erase (ready.begin ());
}

void time_handler (int sig)
{
  int ret_val = sigsetjmp(env[running_thread->tid], 1);
  bool did_just_save_bookmark = ret_val == 0;
  if (did_just_save_bookmark)
    {
      if (!ready.empty ())
        {
          switch_threads (READY);
        }
      jump_to_thread ();
    }
}

void init_main_thread ()
{
  main_thread = (thread *) malloc (sizeof (thread));
  all_threads[0].tid = 0;
  all_threads[0].quantums = 1;
  available_id[0] = 1;
  all_threads[0].state = RUNNING;
  total_quantum++;
  main_thread = &all_threads[0];
  running_thread = (thread *) malloc (sizeof (thread));
  running_thread = main_thread;
}

void timer_restart ()
{
  if (setitimer (ITIMER_VIRTUAL, &timer, nullptr))//TODO: CHECK IF NULLPTR OR NULL
    {
      std::cerr << std::string (SYSTEM_ERROR_MSG) +
                   SETITIMER_ERROR << std::endl;
    }
}

void init_timer (int quantum_usecs)
{
  struct sigaction sa = {0};
  sa.sa_handler = &time_handler;
  if (sigaction (SIGVTALRM, &sa, nullptr) < 0)//TODO: CHECK IF NULLPTR OR NULL
    {
      std::cerr << std::string (SYSTEM_ERROR_MSG) +
                   SIGACTION_ERROR << std::endl;
    }
  // initialize time
//  int seconds = quantum_usecs / SECOND;
//  int useconds = quantum_usecs - seconds * SECOND;//TODO
  int seconds = 0;
  int useconds = 0;
  if (quantum_usecs >= SECOND)
    {
      seconds = quantum_usecs / SECOND;
      useconds = quantum_usecs % SECOND;
    }
  else
    {
      useconds = quantum_usecs;
    }
  timer.it_value.tv_sec = seconds;
  timer.it_value.tv_usec = useconds;
  // keep timer running and repeat
  timer.it_interval.tv_sec = seconds;
  timer.it_interval.tv_usec = useconds;
  timer_restart ();
}

int uthread_init (int quantum_usecs)
{
  if (quantum_usecs <= 0)
    {
      std::cerr << std::string (THREAD_LIBRARY_ERROR) +
                   NEGATIVE_QUANTUM_ERROR << std::endl;
      return -1;
    }
  init_timer (quantum_usecs);
  init_main_thread ();
  sigsetjmp(env[running_thread->tid], 1);
  sigemptyset (&env[running_thread->tid]->__saved_mask);
  return 0;
}

//---------------------------------------------------------------------------------------

int min_available ()
{
  for (int i = 0; i < MAX_THREAD_NUM; i++)
    {
      if (!available_id[i])
        {
          return i;
        }
    }
  return -1;
}

int set_up_thread (thread_entry_point entry_point, thread *new_thread)
{
  char *stack = new_thread->stack;
  address_t sp = (address_t) stack + STACK_SIZE - sizeof (address_t);
  address_t pc = (address_t) entry_point;
  sigsetjmp(env[new_thread->tid], 1);
  (env[new_thread->tid]->__jmpbuf)[JB_SP] = translate_address (sp);
  (env[new_thread->tid]->__jmpbuf)[JB_PC] = translate_address (pc);
  sigemptyset (&env[new_thread->tid]->__saved_mask);
  mask_signal (SIG_UNBLOCK);
  return new_thread->tid;
}

int create_new_thread (thread_entry_point entry_point, int new_thread_id)
{
  auto *new_thread = (thread *) malloc (sizeof (thread));
  new_thread->tid = new_thread_id;
  available_id[new_thread->tid] = 1;
  new_thread->quantums = 0;
  new_thread->sleep_time = 0;
  new_thread->entryPoint = entry_point;
  new_thread->state = READY;
  ready.push_back (new_thread->tid);
  all_threads[new_thread_id] = *new_thread;
  pointers_to_free.push_back (new_thread);
  return set_up_thread (entry_point, new_thread);
}

int uthread_spawn (thread_entry_point entry_point)
{
  if (entry_point == nullptr)
    {
      std::cerr << std::string (THREAD_LIBRARY_ERROR) +
                   ENTRY_POINT_ERROR << std::endl;
      return -1;
    }
  int new_thread_id = min_available ();
  if (new_thread_id == -1)
    {
      std::cerr << std::string (THREAD_LIBRARY_ERROR) +
                   NUM_THREADS_ERROR << std::endl;
      return -1;
    }
  mask_signal (SIG_BLOCK);
  return create_new_thread (entry_point, new_thread_id);

}


//---------------------------------------------------------------------------------------



void terminate_blocked_or_ready_thread (int tid)
{
  auto in_ready = find (ready.begin (), ready.end (), tid);
  if (in_ready != ready.end ())
    {
      ready.erase (in_ready);
      available_id[tid] = 0;
      all_threads[tid].state = NOTHING;
    }
  if (all_threads[tid].state == BLOCKED)
    {
      if (all_threads[tid].sleep_time != 0)
        {
          erase_thread_from_sleep (tid);
        }
      else
        {
          auto in_blocked = find (blocked.begin (), blocked.end (), tid);
          if (in_blocked != blocked.end ())
            {
              blocked.erase (in_blocked);
              all_threads[tid].state = NOTHING;
              available_id[tid] = 0;
            }
        }
    }
}

void terminate_current_thread ()
{
  if (!ready.empty ())
    {
      mask_signal (SIG_UNBLOCK);
      timer_restart ();
      switch_threads (NOTHING);
      jump_to_thread ();
    }
}

int uthread_terminate (int tid)
{
  if (tid >= MAX_THREAD_NUM || tid < 0 || available_id[tid] == 0
      || all_threads[tid].state == NOTHING)
    {
      std::cerr << std::string (THREAD_LIBRARY_ERROR) +
                   THREAD_DOESNT_EXIST_ERROR << std::endl;
      return -1;
    }
  if (tid == 0)//main thread
    {
      for (auto thd: pointers_to_free)
        {
          free (thd);
        }
      exit (0);
    }
  mask_signal (SIG_BLOCK);
  if (running_thread->tid == tid)//running thread
    {
      terminate_current_thread ();
    }
  else
    {//thread in ready/ blocked and possibly in sleep
      terminate_blocked_or_ready_thread (tid);
    }
  mask_signal (SIG_UNBLOCK);
  return 0;
}

void remove_from_blocked (int tid)
{
  auto to_erase_blocked = find (blocked.begin (), blocked.end (), tid);
  if (to_erase_blocked != blocked.end ())
    {
      blocked.erase (to_erase_blocked);
    }
}

//---------------------------------------------------------------------------------------

void set_and_jump (int curr_id)//TODO
{
  int ret_val = sigsetjmp(env[curr_id], 1);
  bool did_not_just_save_bookmark = ret_val != 0;
  if (!did_not_just_save_bookmark)
    {
      jump_to_thread ();
    }
}

int uthread_block (int tid)
{
  if (tid == 0)
    {
      std::cerr << std::string (THREAD_LIBRARY_ERROR) +
                   BLOCK_MAIN_THREAD_ERROR << std::endl;
      return -1;
    }
  if (tid >= MAX_THREAD_NUM || tid < 0 || !available_id[tid]
      || all_threads[tid].state == NOTHING)
    {
      std::cerr << std::string (THREAD_LIBRARY_ERROR) +
                   THREAD_DOESNT_EXIST_ERROR << std::endl;
      return -1;
    }
  mask_signal (SIG_BLOCK);
  //no need to block a blocked thread
  auto is_blocked = find (blocked.begin (), blocked.end (), tid);
  if (is_blocked != blocked.end ())
    {
      mask_signal (SIG_UNBLOCK);
      return 0;
    }
  if (all_threads[tid].state == RUNNING)
    {
      blocked.push_back (tid);
      switch_threads (BLOCKED);
      mask_signal (SIG_UNBLOCK);
      timer_restart ();
      set_and_jump (tid);
    }
  else if (all_threads[tid].state == READY)
    {//sleep&block
      blocked.push_back (tid);
      all_threads[tid].state = BLOCKED;
      auto in_ready = find (ready.begin (), ready.end (), tid);
      if (in_ready != ready.end ())
        {
          ready.erase (in_ready);
        }
    }
  else if (all_threads[tid].state == BLOCKED)
    {//sleep & now blocked
      blocked.push_back (tid);
    }
  mask_signal (SIG_UNBLOCK);
  return 0;
}
//--------------------------------------------------------------------------
void thread_finish_sleep (int tid)
{
  if (all_threads[tid].sleep_time == 0)
    {
      all_threads[tid].state = READY;
      ready.push_back (tid);
    }
}
int uthread_resume (int tid)
{
  if (tid >= MAX_THREAD_NUM || tid < 0 || !available_id[tid]
      || all_threads[tid].state == NOTHING)
    {
      std::cerr << std::string (THREAD_LIBRARY_ERROR) +
                   THREAD_DOESNT_EXIST_ERROR << std::endl;
      return -1;
    }
  if (all_threads[tid].state != BLOCKED)
    {
      return 0;
    }
  remove_from_blocked (tid);
  thread_finish_sleep (tid);
  return 0;

}
//---------------------------------------------------------------------------
int handle_sleep (int num_quantums)
{
  mask_signal (SIG_BLOCK);
  int tid = running_thread->tid;
  all_threads[tid].sleep_time = num_quantums;
  sleeping.push_back (tid);
  switch_threads (BLOCKED);
  mask_signal (SIG_UNBLOCK);
  return tid;
}

int uthread_sleep (int num_quantums)
{
  if (running_thread->tid == 0)
    {
      std::cerr << std::string (THREAD_LIBRARY_ERROR) +
                   MAIN_THREAD_SLEEP_ERROR << std::endl;
      return -1;
    }
  int tid = handle_sleep (num_quantums);
  timer_restart ();
  set_and_jump (tid);
  return 0;
}

//---------------------------------------------------------------------------------------

int uthread_get_tid ()
{
  return running_thread->tid;
}

//---------------------------------------------------------------------------------------
int uthread_get_total_quantums ()
{
  return total_quantum;
}

//---------------------------------------------------------------------------------------
int uthread_get_quantums (int tid)
{
  if (tid == 0)
    {
      return main_thread->quantums;
    }
  if (tid >= MAX_THREAD_NUM || tid < 0 || !available_id[tid]
      || all_threads[tid].state == NOTHING)
    {
      std::cerr << std::string (THREAD_LIBRARY_ERROR) +
                   THREAD_QUANTUMS_ERROR << std::endl;
      return -1;
    }
  return all_threads[tid].quantums;
}



