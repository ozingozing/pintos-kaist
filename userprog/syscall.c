#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address (void *addr);
struct file *get_file_from_fd (int fd);
int add_file_to_fd_table (struct file *file);
bool sys_create(const char *file, unsigned initial_size);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

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
		halt();
		break;
	case SYS_EXIT:							// 프로그램 종료 후 상태 반환
		exit(f->R.rdi);
		break;
	// 	break;
	// // case SYS_FORK:							// 자식 프로세스 생성
	// 	fork(f->R.rdi);
	// case SYS_EXEC:							// 새 프로그램 실행
	// 	exec(f->R.rdi);
	// case SYS_WAIT:							// 자식 프로세스가 종료될 때까지 기다림
	// 	wait(f->R.rdi);		
	case SYS_CREATE:
        sys_create(f->R.rdi, f->R.rsi);
		break;
	// case SYS_REMOVE:						// 파일 삭제
	// 	remove(f->R.rdi);
	case SYS_OPEN:							// 파일 열기
		f->R.rax = open(f->R.rdi);
		break;
	// case SYS_FILESIZE:						// 파일 사이즈 반환
	// 	filesize(f->R.rdi);
	// case SYS_READ:							// 파일에서 데이터 읽기
	// 	read(f->R.rdi, f->R.rsi, f->R.rdx);
	case SYS_WRITE:							// 파일에 데이터 쓰기
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	// case SYS_SEEK:							// 파일의 읽기/쓰기 포인터 이동
	// 	seek(f->R.rdi, f->R.rsi);
	// case SYS_TELL:							// 파일의 현재 읽기/쓰기 데이터 반환
	// 	tell(f->R.rdi);
	// case SYS_CLOSE:							// 파일 닫기
	// 	close(f->R.rdi);
	default:
		printf ("system call!\n");
		thread_exit ();
	}
	// printf ("system call!\n");
	// struct thread *t = thread_current();
	// printf("thread name:%s\n",t->name);
	// thread_exit ();
}

/*User memory access는 이후 시스템 콜 구현할 때 메모리에 접근할 텐데, 
이때 접근하는 메모리 주소가 유저 영역인지 커널 영역인지를 체크*/
void check_address (void *addr){
	struct thread *t = thread_current();
	
	/*포인터가 가리키는 주소가 유저영역의 주소인지 확인 
	|| 포인터가 가리키는 주소가 유저 영역 내에 있지만 
	페이지로 할당하지 않은 영역일수도 잇으니 체크*/
	if (!is_user_vaddr(addr) || addr == NULL || pml4_get_page(t->pml4, addr) == NULL){ 	
		exit(-1);	// 잘못된 접근일 경우 프로세스 종료
	}
}

bool sys_create(const char *file, unsigned initial_size)
{
	check_address(file);
	return filesys_create(file, initial_size);
}


int open(const char *file)
{
	check_address((void *)file);
	struct file *f = filesys_open(file);

	if(f == NULL)
		return -1;
	return add_file_to_fd_table(f);
}

//해당 파일을 파일 디스크립터 배열에 추가
int add_file_to_fd_table (struct file *file)
{
	struct thread *t = thread_current();
    for (int i = 2; i < 10; i++) {
        if (t->fd_table[i] == NULL) {
            t->fd_table[i] = file;
            return i;
        }
    }
    return -1;
}


/* pintos 종료시키는 함수 */
void halt(void){
	// printf("halt 실행됐고 pintos 종료\n");
	// filesys_done();
	power_off();
}

/* 현재 프로세스를 종료시키는 시스템 콜 */
void exit(int status){
	struct thread *t = thread_current();
	t->exit_status = status;
	printf("%s: exit(%d)\n", t->name, t->exit_status);
	thread_exit();
}

int write(int fd, const void *buffer, unsigned size)
{
	if (fd == 1)
	{
		putbuf(buffer, size);
		return size;
	}
	else
	{
		struct file *f = get_file_from_fd(fd);

		if(f == NULL)
			return -1;
		int byte_written = file_write(f, buffer, size);
		return byte_written;
	}
	return -1;
}

//fd배열에서 file 가져오기
struct file *get_file_from_fd (int fd)
{
	struct thread *t = thread_current();
	if(fd < 2 || fd >= 1024)
		return NULL;
	return t->fd_table[fd];
}

// /*파일을 제거하는 함수, 
// 이 때 파일을 제거하더라도 그 이전에 파일을 오픈했다면 
// 해당 오픈 파일은 close 되지 않고 그대로 켜진 상태로 남아있는다.*/
// bool remove (const char *file) {
// 	check_address(file);
// 	if (filesys_remove(file)) {
// 		return true;
// 	} else {
// 		return false;
// 	}
// }