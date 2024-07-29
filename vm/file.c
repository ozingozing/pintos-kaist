/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "string.h"
#include "threads/mmu.h"
#include "devices/disk.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	struct lazy_load_info *info = (struct lazy_load_info*)page->uninit.aux;
	file_page->file = info->file;
	file_page->offset = info->ofs;
	file_page->read_bytes = info->read_bytes;
	file_page->zero_bytes = info->zero_bytes;
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
	struct file *file = file_page->file;

	off_t ofs = file_page->offset;
	int page_read_bytes = file_page->read_bytes;
	int page_zero_bytes = file_page->zero_bytes;

	file_seek(file, ofs);

	page_read_bytes = (int)file_read(file, page->frame->kva, page_read_bytes);

	memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	page->swapped = false;

	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	if(pml4_is_dirty(thread_current()->pml4, page->va))
	{	
		file_write_at(file_page->file, page->frame->kva, file_page->read_bytes, file_page->offset);
		pml4_set_dirty(thread_current()->pml4, page->va, false);
	}

	page->frame = NULL;
	page->swapped = true;
	pml4_clear_page(thread_current()->pml4, page->va);

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	struct thread *t = thread_current();

	if(pml4_is_dirty(t->pml4, page->va))
	{
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->offset);
		pml4_set_dirty(t->pml4, page->va, false);
	}

	pml4_clear_page(t->pml4, page->va);
}

static bool
lazy_load_segment_by_file (struct page *page, void *aux) {
	
	// if (page->frame->kva == NULL)
	// 	return false;

	struct file_page *info = (struct file_page*)aux;
	struct file *file = info->file;
	
	size_t offset = info->offset;
	size_t page_read_bytes = info->read_bytes;
	size_t page_zero_bytes = info->zero_bytes;
	
	// read_at으로 하니 필요 없을 듯
	file_seek (file, offset); 

	/* Do calculate how to fill this page.
	 * We will read PAGE_READ_BYTES bytes from FILE
	 * and zero the final PAGE_ZERO_BYTES bytes. */
	if (file_read(file, page->frame->kva, page_read_bytes) != (int) page_read_bytes) {
		return false;
	}
	// off_t read_byte = 0;
	// read_byte = file_read_at(file, page->frame->kva, page_read_bytes, offset);
	// if(read_byte != page_read_bytes) 
	// 	return false;

	// 읽어야 할 길이가 PGSIZE의 배수가 아닌 경우
	// stick out 조치
	memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);

	struct file_page *file_page = &page->file;
	file_page->offset = offset;
	file_page->read_bytes = page_read_bytes;
	file_page->zero_bytes = page_zero_bytes;
	file_page->file = file;

	return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	
	// file_reopen을 사용하는 이유
	/* 
		// 일관성과 안전성:
		// file_reopen을 사용하면 이미 열려 있는 파일에 대해 새로운 파일 디스크립터가 생성됩니다. 
		// 이는 시스템 내 다른 부분에서 사용될 수 있는 원래 파일 디스크립터와의 충돌을 피할 수 있어 더 안전합니다.

		// 독립적인 파일 포인터:
		// 파일을 다시 열면 별도의 파일 디스크립터가 생성되어 파일 포인터를 독립적으로 조작할 수 있습니다. 
		// 이는 멀티스레드 또는 멀티프로세스 환경에서 파일의 다른 위치에서 동시에 읽기 또는 쓰기가 필요할 때 중요합니다.
		
		// 부작용 방지:
		// 파일을 다시 여는 것은 원래 파일 디스크립터에 영향을 주지 않으므로 부작용을 피할 수 있습니다. 
		// 예를 들어, 다시 열린 파일에서 파일 위치를 변경해도 원래 파일 디스크립터의 파일 위치에 영향을 주지 않습니다. 
		// 이는 올바른 프로그램 동작을 보장하는 데 중요할 수 있습니다.
	 */
	
	// 파일을 다시 여는 것의 잠재적 문제
	/*
		// 자원 관리:
		// 파일을 다시 여는 것은 추가 파일 디스크립터를 소비하며, 
		// 이는 제한된 시스템 자원입니다. 파일 디스크립터가 고갈되지 않도록 적절히 관리해야 합니다.

		// 동시성 제어:
		// 파일이 동시에 접근되는 환경에서는 경합 조건이나 불일치를 방지하기 위해 올바른 동기화 메커니즘을 보장하는 것이 중요합니다.
	*/
	
	// file_open을 사용하지 않는 이유
	/*
		// 이미 열린 파일에 대해 file_open을 사용하면 다음과 같은 문제가 발생할 수 있습니다:

		// 파일 위치 간섭:

		// 동일한 파일을 가리키는 여러 파일 디스크립터가 동일한 파일 위치를 공유하는 경우 서로 간섭할 수 있습니다. 예를 들어, 한 디스크립터에서 읽으면 다른 디스크립터의 위치가 변경될 수 있습니다.
		// 잠금 문제:

		// 파일이 잠금의 대상인 경우 파일을 다시 여는 것이 의도된 잠금 동작을 우회하여 동기화 문제를 일으킬 수 있습니다. 
	*/

	struct file *reopened_file = file_reopen(file);
	if (file_length(reopened_file) - offset <= 0) 
		return NULL;

	size_t temp_length = length < file_length(reopened_file) ? length : file_length(reopened_file);
	size_t temp_zero_length = PGSIZE - (temp_length % PGSIZE);
	void * current_addr = addr;
	file_seek(reopened_file, offset);

	while (temp_length > 0 || temp_zero_length > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = temp_length < PGSIZE ? temp_length : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		
		struct file_page *aux = malloc(sizeof(struct file_page));
		if (aux == NULL)
			return NULL;
		
		aux->file = reopened_file;
		aux->offset = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;
		aux->has_next = temp_length > PGSIZE;


		if( !vm_alloc_page_with_initializer(VM_FILE, current_addr, writable, lazy_load_segment_by_file, aux) ){	
			free(aux);
			return NULL;
		}
		
		/* Advance. */
		temp_length -= page_read_bytes;
		temp_zero_length -= page_zero_bytes;
		current_addr += PGSIZE;
		offset += PGSIZE;
	}

	return addr;
}


/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread *t = thread_current();
	struct page *page = spt_find_page(&t->spt, addr);
	
	if (!page)
		return;
	
	bool has_next;
	do {
		has_next = page->file.has_next;
		spt_remove_page(&t->spt, page);
		addr += PGSIZE;
	} while (has_next && (page = spt_find_page(&t->spt, addr)));
}
