#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"

#include "threads/fixed_point.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

#define NESTING_DEPTH 8
/////////////////////////////////////////////////////////////////////////////////////
/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

//슬립리스트 연산을 위한 락(티켓권 같은거임)
static struct lock sleep_lock;

/////////////////////////////////////////////////////////////////////////////////////

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

//추가추가
static real load_avg;
/////////

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

static bool tick_less (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);
/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
/* 현재 실행 중인 코드를 스레드로 변환하여 스레드 시스템을 초기화합니다.
   일반적으로는 작동하지 않지만, 이번 경우에는 loader.S가 스택의 바닥을 페이지 경계에
   맞추도록 주의했기 때문에 가능합니다.

   또한 실행 큐와 tid 락을 초기화합니다.

   이 함수를 호출한 후, thread_create()로 스레드를 생성하기 전에 페이지 할당자를
   반드시 초기화해야 합니다.

   이 함수가 끝나기 전까지는 thread_current()를 호출하는 것이 안전하지 않습니다. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	lock_init(&sleep_lock);

	//리스트 조기화
	list_init (&ready_list);
	list_init(&sleep_list);
	list_init (&destruction_req);
	list_init(&all_list);
	list_init(&child_list);
	
	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->wakeup_tick = -1;
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
	list_push_front(&all_list, &initial_thread->all_elem);
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
/* NAME이라는 새로운 커널 스레드를 주어진 초기 PRIORITY로 생성하며,
   AUX를 인자로 전달하여 FUNCTION을 실행합니다. 그리고 이를 준비 큐에 추가합니다.
   새로운 스레드의 스레드 식별자를 반환하며, 생성에 실패하면 TID_ERROR를 반환합니다.

   thread_start()가 호출된 이후에는 새로운 스레드가 thread_create()가 반환되기 전에
   스케줄링될 수 있습니다. 심지어 새로운 스레드가 thread_create()가 반환되기 전에
   종료될 수도 있습니다. 반대로, 원래 스레드는 새로운 스레드가 스케줄링되기 전에
   어느 정도의 시간 동안 실행될 수 있습니다. 순서를 보장해야 한다면 세마포어나 다른 형태의
   동기화를 사용해야 합니다.

   제공된 코드는 새로운 스레드의 `priority` 멤버를 PRIORITY로 설정하지만, 실제 우선순위
   스케줄링은 구현되지 않았습니다. 우선순위 스케줄링은 문제 1-3의 목표입니다. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;
	
	
	if(name != "idle")
	{
		list_push_front(&all_list, &t->all_elem);
	}
	if(aux != NULL)
	{
		list_push_front(&child_list, &t->child_elem);
		t->fd_table = palloc_get_multiple(PAL_USER | PAL_ZERO, INT8_MAX);
		t->terminated = false;
	}
	/* Add to run queue. */
	thread_unblock (t);
	
	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;	
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_insert_ordered(&ready_list, &t->elem, priority_more, NULL); // 우선순위대로 쓰레드 레디 리스트에 삽입
	t->status = THREAD_READY;
	if(!intr_context())
		preemption();
	intr_set_level (old_level);
}

void
preemption (void) {
	struct list_elem *e;
	struct thread *t;

	if(list_empty(&ready_list) || thread_current() == idle_thread) // 현재 쓰레드가 idle이거나 ready리스트가 비어있다면 우선순위 비교 X
		return;
	e = list_begin(&ready_list);
	t = list_entry(e, struct thread, elem);
	if(t->priority > thread_current()->priority) // 현재 쓰레드가 우선순위 내림차순으로 정렬된 ready리스트의 맨 앞의 우선순위와 비교
		thread_yield(); // 현재 쓰레드의 우선순위가 낮으면 양보
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());
#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	list_remove(&thread_current()->all_elem);
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	list_insert_ordered(&ready_list, &curr->elem, priority_more, NULL);	
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	struct thread *curr = thread_current (); // 현재 실행중인 쓰레드
	if(!thread_mlfqs){
		curr->origin_priority = new_priority; // origin_priority 업데이트
		update_priority(curr); // 변경된 origin_priority와 도네이션 리스트 비교
	}
	else{
		curr->priority = new_priority;
		preemption();
	}
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) {
	/* TODO: Your implementation goes here */
	thread_current()->nice = nice;
	update_nice();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return fixed_to_nearest_integer(load_avg * 100);
}

real
get_decay(){
	real num1 = load_avg * 2;
	real num2 = add_fixed_from_integer(num1, 1);
	return divide_fixed(num1, num2);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	return fixed_to_nearest_integer(thread_current()->recent_cpu * 100);
}

void
thread_set_load_avg(void){
	enum intr_level old_level;
	old_level = intr_disable();
	int ready_threads = list_size(&ready_list);
	if(thread_current() != idle_thread)
		ready_threads++;

	real fifty_nine, sixty, one, num1, num2;
	fifty_nine = integer_to_fixed(59);
	sixty = integer_to_fixed(60);
	one = integer_to_fixed(1); 

	num1 = divide_fixed(fifty_nine, sixty); 
	num1 = multiple_fixed(num1, load_avg);
	num2 = divide_fixed(one, sixty);
	num2 *= ready_threads;
	load_avg = num1 + num2;
	intr_set_level(old_level);
}

void
update_recent_cpu(){
	struct thread *curr = thread_current();
	real decay = get_decay();
	real calulated = 0;
	struct list_elem *e = list_begin(&all_list);

	while(e != list_tail(&all_list)){
		curr = list_entry(e, struct thread, all_elem);
		calulated = multiple_fixed(decay, curr->recent_cpu);
		calulated = add_fixed_from_integer(calulated, curr->nice);
		curr->recent_cpu = calulated;
		e = list_next(e);
	}
}

int
thread_set_recent_cpu_add_one(void)
{
	struct thread *curr;
	if(thread_current() == idle_thread)
		return;
	curr = thread_current();
	enum intr_level old_level;
	old_level = intr_disable();
	curr->recent_cpu = add_one_fixed(curr->recent_cpu);
	intr_set_level(old_level);
}

void
thread_set_wakeup_tick (int64_t tick) {
	thread_current ()->wakeup_tick = tick;
}

/* Returns the current thread's priority. */
int64_t
thread_get_wakeup_tick (void) {
	return thread_current ()->wakeup_tick;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */

/* 	아이들 스레드. 다른 스레드가 실행 준비가 되어 있지 않을 때 실행됩니다.

   	아이들 스레드는 처음에 thread_start()에 의해 준비 리스트에 추가됩니다.
	초기에 한 번 스케줄링되며, 이 때 아이들 스레드를 초기화하고, 
	thread_start()가 계속될 수 있도록 전달된 세마포어를 "up" 합니다. 
	그리고 즉시 블록됩니다. 
	그 후, 아이들 스레드는 준비 리스트에 나타나지 않습니다. 
	준비 리스트가 비어 있을 때 특별한 경우로서 
	next_thread_to_run()에 의해 반환됩니다. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		
		/* 	인터럽트를 다시 활성화하고 다음 인터럽트를 기다립니다.

   			`sti` 명령어는 다음 명령어가 완료될 때까지 인터럽트를 비활성화합니다. 
			그래서 이 두 명령어는 원자적으로 실행됩니다. 
			이 원자성은 중요합니다; 그렇지 않으면, 인터럽트가 다시 활성화되고 
			다음 인터럽트가 발생하기를 기다리는 사이에 인터럽트가 처리될 수 있으며, 
			최대 한 클록 틱의 시간이 낭비될 수 있습니다.

   			참조: [IA32-v2a] "HLT", 
			[IA32-v2b] "STI", 
			[IA32-v3a] 7.11.1 "HLT Instruction". */

		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->nice = 0;
	t->recent_cpu = 0;
	///////////////////////
	t->magic = THREAD_MAGIC;
	
	t->origin_priority = priority; // 원래의 우선순위 설정
	list_init(&t->donations); // 기부해준 쓰레드들을 저장할 쓰레드 초기화
	t->wait_on_lock = NULL; // 초기화
	t->exit_status = 1; // 종료 상태 0이면 잘 끝남 그 외에는 잘 안끝나서 추가 행동 필요
	t->fd = 2; //0표준입력 1표준출력 2는 표준에러인데 pintos에는 없음
	
	sema_init(&t->fork_sema, 0);
	sema_init(&t->when_use_free_curr_sema, 0);
	sema_init(&t->when_use_wait_other_sema, 0);
	sema_init(&t->load, 0);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

void thread_sleep(int64_t end_tick){
	enum intr_level old_level;
	struct thread *cur = thread_current();

	ASSERT(!intr_context());
	ASSERT(cur != idle_thread);

	old_level = intr_disable();
	cur->wakeup_tick = end_tick;

	lock_acquire(&sleep_lock);
	list_insert_ordered(&sleep_list, &cur->elem, tick_less, NULL); 
	lock_release(&sleep_lock); 

	thread_block();

	intr_set_level(old_level);
}

void thread_check_sleep_list(){
	enum intr_level old_level;
	struct thread *t;
	old_level = intr_disable();
	int64_t ticks = timer_ticks();
	

	// lock_acquire(&sleep_lock);
    while (!list_empty(&sleep_list)) { // 슬립 리스트에서 꺠울 쓰레드 탐색
        t = list_entry(list_front(&sleep_list), struct thread, elem); // 슬립 리스트 맨 앞 쓰레드 조회
        if (t->wakeup_tick > ticks) { // 쓰레드가 현재 글로벌 틱보다 크다면 탐색 종료
            break;
        }
        list_pop_front(&sleep_list); // 쓰레드가 현재 글로벌 틱보다 작거나 같다면 슬립 리스트에서 제거
        thread_unblock(t); // 해당 쓰레드 언블록
    }
	// lock_release(&sleep_lock);
	intr_set_level(old_level);
}

/* 쓰레드 wakeup_tick 오름차순 정렬 함수*/
bool
tick_less (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  
  return a->wakeup_tick < b->wakeup_tick;
}

bool
priority_more (const struct list_elem *a_,
			   const struct list_elem *b_,
               void *aux UNUSED) 
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  
  return a->priority > b->priority;
}


bool
donation_priority_more (const struct list_elem *a_, 
						const struct list_elem *b_,
						void *aux UNUSED) 
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  
  return a->priority < b->priority;
}


void
donate_priority(struct thread *holder, struct thread *receiver)
{
	enum intr_level old_level;
	if(holder->priority < receiver->priority)
		holder->priority = receiver->priority;
	old_level = intr_disable();
	list_push_front(&holder->donations, &receiver->d_elem);
	while (holder->wait_on_lock != NULL)
	{
		if(holder->wait_on_lock->holder->priority > holder->priority)
			break;
		holder->wait_on_lock->holder->priority = holder->priority;
		holder = holder->wait_on_lock->holder;
	}
	update_priority(holder);
	intr_set_level(old_level);
}

void donate_priority_nested(struct thread *current_thread)
{				 
	// while (current_thread->wait_on_lock != NULL)
	// {
	// 	if(current_thread->wait_on_lock->holder->priority > current_thread->priority)
	// 		break;
	// 	current_thread->wait_on_lock->holder->priority = current_thread->priority;
	// 	current_thread = current_thread->wait_on_lock->holder;
	// }
	// update_priority(current_thread);	
}

void remove_donation(struct lock *lock)
{
	enum intr_level old_level;
	struct list_elem *current_thread_d_elem = list_begin(&thread_current()->donations);
 
	ASSERT(thread_current() == lock->holder)

	old_level = intr_disable();
	while (current_thread_d_elem != list_tail(&thread_current()->donations))
	{
		if(list_entry(current_thread_d_elem, struct thread, d_elem)->wait_on_lock == lock)
			list_remove(current_thread_d_elem);
		current_thread_d_elem = list_next(current_thread_d_elem);
	}
	update_priority(thread_current());
	intr_set_level(old_level);
}

void update_donation_list(struct lock *_lock)
{
	if(list_empty(&_lock->semaphore.waiters))
		return;
	struct list_elem *e = list_begin(&_lock->semaphore.waiters);
	enum intr_level old_level;
	old_level = intr_disable();
	while (e != list_tail(&_lock->semaphore.waiters))
	{
		if(list_entry(e, struct thread, d_elem)->wait_on_lock == _lock)
			list_push_front(&thread_current()->donations, list_entry(e, struct thread, d_elem));	
		e = list_next(e);
	}
	intr_set_level(old_level);
}

void update_priority(struct thread *t)
{
	if(!list_empty(&t->donations))
	{
		struct thread *have_max_priority_thread 
			= list_entry(list_max(&t->donations, priority_more, NULL), 
						struct thread, 
						d_elem);

		if(t->origin_priority < have_max_priority_thread->priority)
			t->priority = have_max_priority_thread->priority;
		else
			t->priority = t->origin_priority; 
	}
	else
		t->priority = t->origin_priority;

	preemption();
}

void update_nice(void)
{
	struct list_elem *e;
	struct thread *curr;
	int calulated;
	// printf("%d\n",thread_current()->priority);
	for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)){
		curr = list_entry(e, struct thread, all_elem);
		calulated = PRI_MAX - fixed_to_nearest_integer((curr->recent_cpu / 4)) - (curr->nice * 2);
		if (calulated > 63)
			calulated = PRI_MAX;
		else if (calulated < PRI_MIN)
			calulated = PRI_MIN;
		curr->priority = calulated;
		//  printf("%s: nice:%d priorrity:%d , ",curr->name, curr->nice, curr->priority);
	}
	// printf("\n");
}


/* =========== Project 2 - Custom Function =========== */
struct thread *
get_child_thread(tid_t tid) {
	struct thread *curr_thread = thread_current();
	if(!list_empty(&child_list))
	{
		for (struct list_elem *e = list_begin(&child_list); e != list_end(&child_list); e = list_next(e)) {
			struct thread *child_thread = list_entry(e, struct thread, child_elem);
			if (child_thread->tid == tid) {
				return child_thread;
			}
		}
	}
	return NULL;
}
