/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include <string.h>
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
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
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

	return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	
	struct file *f = file_reopen(file);
	size_t temp_length = length < file_length(f) ? length : file_length(f);
	size_t temp_zero_length = PGSIZE - temp_length % PGSIZE;
	// if((temp_length + temp_zero_length) % PGSIZE != 0) return NULL;
	// if(offset % PGSIZE != 0) return NULL;

	void * current_addr = addr;

	file_seek(file, offset);
	while (temp_length > 0 || temp_zero_length > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = temp_length < PGSIZE ? temp_length : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		
		struct file_page *aux = malloc(sizeof(struct file_page));
		if (aux == NULL)
			return NULL;
		
		aux->file = f;
		aux->offset = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;

		if( !vm_alloc_page_with_initializer(VM_FILE, current_addr, writable, lazy_load_segment_by_file, aux) ){	
			free(aux);
			return NULL;
		}
		
		/* Advance. */
		temp_length -= page_read_bytes;
		temp_zero_length -= page_zero_bytes;
		current_addr += PGSIZE;
		/*
			파일에서 데이터를 읽어올 때 파일 오프셋을 적절히 이동시키기 위해서이다.
			load_segment 함수는 파일의 특정 오프셋부터 시작하여 세그먼트를 로드한다. 
			이때 세그먼트의 크기가 페이지 크기보다 클 경우, 여러 페이지에 걸쳐서 세그먼트를 로드해야 한다.
			각 반복마다 page_read_bytes 만큼의 데이터를 파일에서 읽어와 페이지에 로드하고,
			이 때 파일 오프셋 ofs를 page_read_bytes 만큼 증가시켜야 다음 페이지를 로드할 때 파일의 올바른 위치에서 데이터를 읽어올 수 있다.
		*/
		offset += page_read_bytes;
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
	
	do {
		spt_remove_page(&t->spt, page);
		addr += PGSIZE;
	} while ((page = spt_find_page(&t->spt, addr)));
}
