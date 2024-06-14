#include "userprog/syscall.h"
#include <stdio.h>
#include "lib/stdio.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "threads/palloc.h"

struct lock local_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address (void *addr);
bool fd_is_valid(int fd);
bool file_is_valid(char *file);

struct file *get_file_from_fd (int fd);
int add_file_to_fd_table (struct file *file);

bool sys_create(const char *file, unsigned initial_size);
int sys_open(const char *file);
int sys_filesize (int fd);
int sys_read (int fd, void *buffer, unsigned length);
int sys_write(int fd, const void *buffer, unsigned size);
bool sys_remove (const char *file);
void sys_close(int fd);
void sys_exit(int status);
void sys_halt(void);
void sys_seek (int fd, unsigned position);
unsigned sys_tell (int fd);
int sys_exec (const char *cmd_line);


void lock_acquire_if_available(const struct lock *lock);
void lock_release_if_available(const struct lock *lock);
/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */


typedef int pid_t;

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
	
}

/* The main system call interface */
/*syscall_handler를 호출할 때 이미 인터럽트 프레임에 
해당 시스템 콜 넘버에 맞는 인자 수만큼 들어있다. 
그러니 각 함수별로 필요한 인자 수만큼 인자를 넣어준다. 
이때 rdi, rsi, ...얘네들은 특정 값이 있는 게 아니라 그냥 인자를 담는 그릇의 번호 순서이다. 
어떤 특정 인자와 매칭되는 게 아니라 
첫번째 인자면 rdi, 두번째 인자면 rsi 이런 식이니 헷갈리지 말 것.
*/
void
syscall_handler (struct intr_frame *f UNUSED) {
	int sys_number = f->R.rax;

	switch (sys_number)
	{
	case SYS_HALT: 							// 운영체제 종료
		sys_halt();
		break;
	case SYS_EXIT:							// 프로그램 종료 후 상태 반환
		sys_exit(f->R.rdi);
		break;
	case SYS_FORK:							// 자식 프로세스 생성
		thread_current()->pre_if = f;
		f->R.rax = fork(f->R.rdi);
		break;
	case SYS_EXEC:							// 새 프로그램 실행
		f->R.rax = sys_exec(f->R.rdi);
		break;
	case SYS_WAIT:							// 자식 프로세스가 종료될 때까지 기다림
		f->R.rax = wait(f->R.rdi);		
		break;
	case SYS_CREATE:
        f->R.rax = sys_create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:						// 파일 삭제
		 f->R.rax = sys_remove(f->R.rdi);
		break;
	case SYS_OPEN:							// 파일 열기
		f->R.rax = sys_open(f->R.rdi);
		break;
	case SYS_FILESIZE:						// 파일 사이즈 반환
		f->R.rax = sys_filesize(f->R.rdi);
		break;
	case SYS_READ:							// 파일에서 데이터 읽기
		f->R.rax = sys_read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:							// 파일에 데이터 쓰기
		f->R.rax = sys_write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:							// 파일의 읽기/쓰기 포인터 이동
		sys_seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:							// 파일의 현재 읽기/쓰기 데이터 반환
		sys_tell(f->R.rdi);
		break;
	case SYS_CLOSE:							// 파일 닫기
		sys_close(f->R.rdi);
		break;
	}
	// printf ("system call!\n");
	// struct thread *t = thread_current();
	// printf("thread name:%s\n",t->name);
	// thread_exit ();
}

/*User memory access는 이후 시스템 콜 구현할 때 메모리에 접근할 텐데, 
이때 접근하는 메모리 주소가 유저 영역인지 커널 영역인지를 체크*/
void check_address (void *addr)
{
	struct thread *t = thread_current();
	
	/*포인터가 가리키는 주소가 유저영역의 주소인지 확인 
	|| 포인터가 가리키는 주소가 유저 영역 내에 있지만 
	페이지로 할당하지 않은 영역일수도 잇으니 체크*/
	if (!is_user_vaddr(addr) || addr == NULL || pml4_get_page(t->pml4, addr) == NULL){ 	
		sys_exit(-1);	// 잘못된 접근일 경우 프로세스 종료
	}
}

/* file 만들기 */
bool sys_create(const char *file, unsigned initial_size)
{
	check_address(file);
	if(!file_is_valid(file))
		sys_exit(-1);
	lock_acquire_if_available(&filesys_lock);
	bool result = filesys_create(file, initial_size);
	lock_release_if_available(&filesys_lock);
	return result;
}


/* file 열기 */
int sys_open(const char *file)
{
	
	check_address((void *)file);

	lock_acquire_if_available(&filesys_lock);
	struct file *f = filesys_open(file);
	lock_release_if_available(&filesys_lock);

	if(f == NULL) {
		return -1;
	}
	int fd =  add_file_to_fd_table(f);
	if(fd == -1)
		file_close(f);

	return fd;
}

/* fd에 해당하는 파일 크기 들고오기 */
int sys_filesize (int fd)
{
	struct file *f = get_file_from_fd(fd);
	if(!file_is_valid(fd))
	{
		sys_exit(-1);
		return -1;
	}


	if(f == NULL) return -1;
	return file_length(f);
}

/* file 읽기 */
int sys_read (int fd, void *buffer, unsigned length)
{
	check_address(buffer);
	// lock_acquire(&filesys_lock);
	if(!fd_is_valid(fd))
	{
		// lock_release(&filesys_lock);
		sys_exit(-1);
		return -1;
	}


	// lock_acquire(&filesys_lock);
	unsigned i;

	switch (fd)
	{
	case STDIN_FILENO:
		lock_acquire_if_available(&filesys_lock);
        i = input_getc();
		lock_release_if_available(&filesys_lock);
		break;
	case STDOUT_FILENO:
		i = -1;
		break;

	default:
		{
			// 파일에서 읽기
        	struct file *file = get_file_from_fd(fd);
        	if (file == NULL)
			{
				printf("sys_read()파일이 없음\n");
				// lock_release(&filesys_lock);
				return -1;
			}
			lock_acquire_if_available(&filesys_lock);
			i = file_read(file, buffer, length);
			lock_release_if_available(&filesys_lock);
		}
		break;
	}
	// lock_release(&filesys_lock);
	return i;
    // if (fd == STDIN_FILENO)//표준입력 받기
	// {
    //     unsigned i;
	// 	lock_acquire(&filesys_lock);
    //     for (i = 0; i < length; i++)
	// 	{
    //         ((uint8_t *)buffer)[i] = input_getc();
    //     }
	// 	lock_release(&filesys_lock);
    //     return i;
    // } 
	// else if(fd == STDOUT_FILENO) {
	// 	// lock_release(&filesys_lock);
	// 	return -1;
	// }
	// else if (fd > 1 && fd < INT8_MAX) 
	// {
    //     // 파일에서 읽기
    //     struct file *file = get_file_from_fd(fd);
    //     if (file == NULL){
	// 		// lock_release(&filesys_lock);
	// 		return -1;
	// 	}
    //     lock_acquire(&filesys_lock);
	// 	int bytes_read = file_read(file, buffer, length);
	// 	lock_release(&filesys_lock);
    //     if (bytes_read < 0) {
	// 		// lock_release(&filesys_lock);
	// 		return -1;
	// 	}
	// 	// lock_release(&filesys_lock);
    //     return bytes_read;
    // }
	// // lock_release(&filesys_lock);
    // return -1;  // 기본적으로 실패 반환
}

/* file 쓰기 */
int sys_write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	// lock_acquire(&filesys_lock);
	if(!fd_is_valid(fd))
	{
		// lock_release(&filesys_lock);
		sys_exit(-1);
		return -1;
	}

	

	switch (fd)
	{
	case STDIN_FILENO:
		size = -1;
		break;
		
	case STDOUT_FILENO:
		lock_acquire_if_available(&filesys_lock);
		putbuf(buffer, size);
		lock_release_if_available(&filesys_lock);
		break;

	default:
        {
			lock_acquire_if_available(&filesys_lock);
            struct file *f = get_file_from_fd(fd);
            if (f == NULL) {
                // lock_release(&filesys_lock);
                return -1;
            }
            size = file_write(f, buffer, size);
			lock_release_if_available(&filesys_lock);
        }
        break;
	}
	return size;

	// if(fd == STDIN_FILENO) {//0이면 표준 입력이니 -1 오류 리턴
	// 	// lock_release(&filesys_lock);
	// 	return -1; 
	// }
	// else if (fd == STDOUT_FILENO) //1이면 표준출력
	// {
	// 	lock_acquire(&filesys_lock);
	// 	putbuf(buffer, size);
	// 	lock_release(&filesys_lock);
	// 	return size;
	// }
	// else
	// {
	// 	struct file *f = get_file_from_fd(fd);
	// 	if(f == NULL) {
	// 		// lock_release(&filesys_lock);
	// 		return -1;
	// 	}
	// 	lock_acquire(&filesys_lock);
	// 	int write_byte = file_write(f, buffer, size);
	// 	lock_release(&filesys_lock);
	// 	return write_byte;
	// }
	// // lock_release(&filesys_lock);
	// return -1;
}

/* seek 시스템 콜은 파일의 현재 읽기/쓰기 위치를 
지정된 위치로 이동시키는 역할을 합니다. 
이는 파일 포인터를 변경하여 다음 
읽기나 쓰기가 지정된 위치에서 시작되도록 합니다. */
void sys_seek (int fd, unsigned position)
{
	if(!fd_is_valid(fd))
	{
		sys_exit(-1);
		return -1;
	}
	file_seek(get_file_from_fd(fd), position);
}

/* tell 시스템 콜은 현재 파일 포인터의 위치를 반환합니다. 
이는 파일 내에서 현재 읽기/쓰기 위치를 알려줍니다. */
unsigned sys_tell (int fd)
{
	if(!fd_is_valid(fd))
	{
		sys_exit(-1);
		return -1;
	}
	return file_tell(get_file_from_fd(fd));
}

/* file 닫기 */
void sys_close(int fd)
{
	struct file *f = get_file_from_fd(fd);
	if(f != NULL)
	{
		lock_acquire(&filesys_lock);
		file_close(f);
		remove_fd(fd);
		lock_release(&filesys_lock);
	}
}

/* pintos 종료시키는 함수 */
void sys_halt(void)
{
	power_off();
}

/* 현재 프로세스를 종료시키는 시스템 콜 */
void sys_exit(int status)
{
	struct thread *t = thread_current();
	t->exit_status = status;
	#ifdef USERPROG 
	printf("%s: exit(%d)\n", t->name, t->exit_status);
	#endif
	thread_exit();
}

/*파일을 제거하는 함수, 
이 때 파일을 제거하더라도 그 이전에 파일을 오픈했다면 
해당 오픈 파일은 close 되지 않고 그대로 켜진 상태로 남아있는다.*/
bool sys_remove (const char *file) 
{
	check_address(file);
	if(!file_is_valid(file))
		sys_exit(-1);
	lock_acquire_if_available(&filesys_lock);
	bool result = filesys_remove(file);
	lock_release_if_available(&filesys_lock);
    return result;
}


pid_t fork (const char *thread_name)
{
	return process_fork(thread_name, thread_current()->pre_if);
}

int sys_exec (const char *cmd_line) {
	char *temp = palloc_get_page(PAL_ZERO);
	strlcpy(temp, cmd_line, strlen(cmd_line) + 1);
	// if (!lock_held_by_current_thread(&filesys_lock))
		// lock_acquire(&filesys_lock);
	int result = -1;
	if ((result = process_exec(temp)) == -1)
	{
		sys_exit(-1);
	}
	// if (lock_held_by_current_thread(&filesys_lock))
	// 	lock_release(&filesys_lock);
	return result;
}



int wait(pid_t tid)
{
	return process_wait (tid);
}









/* fd에 해당하는 파일 추가 삭제 들고오기 */

//fd배열에서 file 가져오기
struct file *get_file_from_fd (int fd)
{
	struct thread *t = thread_current();
	if(fd < 2 || fd > INT8_MAX)
		return NULL;
	return t->fd_table[fd];
}

/* 해당 파일을 파일 디스크립터 배열에 추가 */
int add_file_to_fd_table (struct file *file)
{
	struct thread *t = thread_current();
    for (int i = t->fd; i < INT8_MAX; i++) {
        if (t->fd_table[i] == NULL) {
            t->fd_table[i] = file;
			t->fd = i;
            return i;
        }
    }
	t->fd = INT8_MAX;
    return -1;
}

/* fd에 해당하는 파일 제거 */
void remove_fd(int fd) 
{
	struct thread *t = thread_current();
	if(fd > 1 && fd < INT8_MAX)
		return t->fd_table[fd] = NULL;
}

/* fd가 유효한지 확인 */
bool fd_is_valid(int fd)
{
	if(-1 < fd && fd < INT8_MAX) return true;
	else return false;
}

/* 파일이 유효한지 확인 */
bool file_is_valid(char *file)
{
	if(file == "" || file == NULL) return false;
	else return true;
}



void lock_acquire_if_available(const struct lock *lock) {
	if (!lock_held_by_current_thread(lock)) {
		lock_acquire(lock);
	}
}

void lock_release_if_available(const struct lock *lock) {
	if (lock_held_by_current_thread(lock)) {
		lock_release(lock);
	}
}
/* *********************************** */