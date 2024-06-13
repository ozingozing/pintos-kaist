#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/fixed_point.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

// #define USERPROG

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
	
	int64_t wakeup_tick; // 슬립 쓰레드 시간되면 깨우는 틱
	int origin_priority; // 실제 오리진 우선순위
	struct list donations; // 이 쓰레드에 우선순위를 기부하는 쓰레드들의 리스트
	
	struct list_elem d_elem; // donations리스트의 elem
	struct list_elem all_elem; //모든 리스트 관리
	struct list_elem child_elem;
	struct lock *wait_on_lock; // 대기중인 락
	
	struct semaphore fork_sema;
	struct semaphore when_use_wait_other_sema;
	struct semaphore when_use_free_curr_sema;
	struct semaphore load;

	int nice;					//나이스한 녀석 nice지수가 높으면(양수) 양보 잘함 낮으면(음수) 양보 못함
	real recent_cpu;			//최근에 CPU얼마나 썼는지 많이 쓰면 쓸 수록
	int exit_status;
	unsigned fd;				//파일디스크립터 == idx
	struct file **fd_table;//파일을 담고있는 파일디스크립터 테이블

	struct intr_frame *pre_if; //이전 if정보
	struct file *running_file;
	bool terminated;
	struct list child_list;
};


/* sleep 쓰레드 리스트 */
static struct list sleep_list;

/* 모든 리스트 관리 */
static struct list all_list;	

/* Thread destruction requests */
static struct list destruction_req;




/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;
void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void thread_set_wakeup_tick (int64_t tick);
int64_t thread_get_wakeup_tick (void);

void do_iret (struct intr_frame *tf);

void thread_sleep(int64_t end_tick);
void thread_check_sleep_list(int64_t tick);

/* 우선순위 내림차순 정렬*/
bool priority_more (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);
/* 리스트랑 현재 우선순위 비교  리스트에 있는 우선순위가 크면 true*/
bool donation_priority_more (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED); 

/* 우선순위를 기부 */
void donate_priority(struct thread *holder, struct thread *receiver);
/* 연쇄적인 priority chain priority 업데이트 */
void donate_priority_nested(struct thread *current_thread);
/* 락을 가지고 있던 쓰레드가 release되면 그 쓰레드 donations리스트를 날려줘야지 */
void remove_donation(struct lock *lock);
/* 현재 우선순위를 origin priority업데이트 */
void update_priority(struct thread *t);
/* load_avg 1초마다 업데이트 용도 */
void thread_set_load_avg(void);
/* 1초마다 recent_cpu 업데이트 */
void update_recent_cpu();
/* 계산된 decay 가져오기 */
real get_decay();
/* 4틱 마다 모든 쓰레드 nice 재계산 */
void update_nice();

void update_donation_list(struct lock *_lock);

struct thread *get_child_thread(tid_t tid);

void preemption (void);

#endif /* threads/thread.h */
