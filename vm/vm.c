/* vm.c: Generic interface for virtual memory objects. */
/* vm.c: 가상 메모리 객체를 위한 일반 인터페이스 */
#include <stdio.h>
#include "threads/malloc.h"
#include "vm/vm.h"
#include "string.h"
#include "vm/inspect.h"

uint64_t hash_hash_func_impl(const struct hash_elem *e, void *aux) {
	struct page *p = hash_entry(e, struct page, h_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

bool hash_less_func_impl(const struct hash_elem *a_h_elem, const struct hash_elem *b_h_elem, void *aux) {
	struct page *a = hash_entry(a_h_elem, struct page, h_elem);
	struct page *b = hash_entry(b_h_elem, struct page, h_elem);
	return a->va < b->va;
}

void hash_action_func_impl (struct hash_elem *e, void *aux){
	struct page *p = hash_entry(e, struct page, h_elem);
	destroy(p);
	free(p);
}

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
/* 각 하위 시스템의 초기화 코드를 호출하여 가상 메모리 하위 시스템을 초기화합니다. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* 위의 줄들은 수정하지 마십시오. */
	/* TODO: Your code goes here. */
	/* TODO: 여기에 코드를 작성하세요. */
	list_init(&frame_table);
	lock_init(&frame_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
/* 페이지의 유형을 가져옵니다. 이 함수는 페이지가 초기화된 후 그 유형을 알고 싶을 때 유용합니다.
 * 이 함수는 현재 완전히 구현되어 있습니다. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
/* 도우미 함수들 */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
/* 초기화 함수와 함께 보류 중인 페이지 객체를 만듭니다. 
   페이지를 만들고 싶다면 직접 만들지 말고 
   이 함수를 통해 만들거나 
   `vm_alloc_page`를 사용하세요. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	// 다른 페이지로 변화할 uninit page를 만들어 주는것이기 때문에 type이 uninit page로 오는 요청은 에러
	ASSERT (VM_TYPE(type) != VM_UNINIT);

	struct supplemental_page_table *spt = &thread_current ()->spt;

	if(spt_find_page(spt, upage) == NULL)
	{
		struct page *new_page = (struct page*)malloc(sizeof(struct page));
		switch (VM_TYPE(type))
		{
		case VM_ANON:
			uninit_new(new_page, upage, init, VM_ANON, aux, anon_initializer);
			break;
		case VM_FILE:
			uninit_new(new_page, upage, init, VM_FILE, aux, file_backed_initializer);
			break;
		default:
			break;
		}
			new_page->writable = writable;

			if(spt_insert_page(spt, new_page))
				return true;
			else
				goto err;
	}
	err:
		return false;
}

/* Find VA from spt and return page. On error, return NULL. */
/* spt에서 VA를 찾아 페이지를 반환합니다. 오류가 발생하면 NULL을 반환합니다. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page page;
	page.va = pg_round_down(va);
	struct hash_elem *e = hash_find(&spt->hash_table, &page.h_elem);
	return e != NULL ? hash_entry(e, struct page, h_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
/* 유효성을 검증한 후 PAGE를 spt에 삽입합니다. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	return hash_insert(&spt->hash_table, &page->h_elem) == NULL ? true : false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
/* 교체될 프레임 구조체를 가져옵니다. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	 /* TODO: 교체 정책은 당신에게 달려 있습니다. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
/* 한 페이지를 교체하고 해당 프레임을 반환합니다.
 * 오류가 발생하면 NULL을 반환합니다. */
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	/* TODO: victim을 swap out하고 교체된 프레임을 반환합니다. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/* palloc()하여 프레임을 가져옵니다.
   사용 가능한 페이지가 없으면 페이지를 교체하고 
   반환합니다. 이 함수는 항상 유효한 주소를 반환합니다. 
   즉, 사용자 풀 메모리가 가득 차면 
   이 함수는 사용 가능한 메모리 공간을 확보하기 위해 
   프레임을 교체합니다. */
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame*)malloc(sizeof(struct frame));
	/* TODO: Fill this function. */
	frame->kva = palloc_get_page(PAL_USER | PAL_ZERO);
	if(!frame->kva)
	{
		PANIC("TODO .");
		return frame;
	}

	frame->page = NULL;
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
/* 스택 확장 */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
/* 쓰기 보호된 페이지에서의 오류 처리 */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
/* 성공 시 true 반환 */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = spt_find_page(spt, pg_round_down(addr));
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if(addr == NULL || is_kernel_vaddr(addr))
		return false;

	if(not_present)
	{
		if(page == NULL)	
		{
			if(addr < USER_STACK && addr > (thread_current()->rsp - 8))
			{
				PANIC("TODO");
			}
			else
				return false;
		}
	}

	if(write && !page->writable)
		return false;

	return vm_do_claim_page (page);	
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
/* 페이지를 해제합니다.
 * 이 함수를 수정하지 마십시오. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
/* VA에 할당된 페이지를 클레임합니다. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = spt_find_page(&thread_current()->spt, va);
	/* TODO: Fill this function */
	if(!page)
		return false;

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
/* PAGE를 클레임하고 mmu를 설정합니다. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	/* 링크 설정 */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	/* TODO: 페이지 테이블 항목을 삽입하여 페이지의 VA를 프레임의 PA에 매핑합니다. */
	list_push_back(&frame_table, &frame->f_elem);
	return pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable) ? swap_in (page, frame->kva) : false;
}

/* Initialize new supplemental page table */
/* 새로운 보조 페이지 테이블을 초기화합니다. */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->hash_table, hash_hash_func_impl, hash_less_func_impl, NULL);
}

/* Copy supplemental page table from src to dst */
/* src에서 dst로 보조 페이지 테이블을 복사합니다. */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	struct hash_iterator h_iter;
    hash_first(&h_iter, &src->hash_table);
    while (hash_next(&h_iter)) {
        struct page *src_page = hash_entry(hash_cur(&h_iter), struct page, h_elem);

        enum vm_type type = src_page->operations->type;
        void *upage = src_page->va;
        bool writable = src_page->writable;

		if(VM_TYPE(type) == VM_UNINIT)
		{
			struct lazy_load_info *info = (struct lazy_load_info*)malloc(sizeof(struct lazy_load_info));
			if(!vm_alloc_page_with_initializer(VM_ANON, upage, writable, src_page->uninit.init, src_page->uninit.aux))
				return false;
			continue;
		}	

		if(!vm_alloc_page(type, upage, writable))
			return false;
		
		if(!vm_claim_page(upage))
			return false;

		memcpy(spt_find_page(&dst->hash_table, upage)->frame->kva, src_page->frame->kva, PGSIZE);
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
/* 보조 페이지 테이블이 보유한 리소스를 해제합니다. */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	/* TODO: 스레드가 보유한 모든 보조 페이지 테이블을 파괴하고 수정된 모든 내용을 스토리지에 다시 씁니다. */
	hash_clear(&spt->hash_table, hash_action_func_impl);
}
