#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/off_t.h"


struct lock filesys_lock;

void syscall_init (void);

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

void *mmap (void *addr, size_t length, int writable, int fd, off_t offset);
void munmap (void *addr);

#endif /* userprog/syscall.h */
