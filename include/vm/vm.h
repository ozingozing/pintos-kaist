#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

enum vm_type {
	/* page not initialized */
	/* 페이지가 초기화되지 않음 */
	VM_UNINIT = 0,
	/* page not related to the file, aka anonymous page */
	/* 파일과 관련이 없는 페이지, 즉 익명 페이지 */
	VM_ANON = 1,
	/* page that realated to the file */
	/* 파일과 관련된 페이지 */
	VM_FILE = 2,
	/* page that hold the page cache, for project 4 */
	/* 페이지 캐시를 보유한 페이지, 프로젝트 4용 */
	VM_PAGE_CACHE = 3,

	/* Bit flags to store state */
	/* 상태를 저장하기 위한 비트 플래그 */

	/* Auxillary bit flag marker for store information. You can add more
	 * markers, until the value is fit in the int. */
	/* 정보를 저장하기 위한 보조 비트 플래그 마커. 값을 int에 맞출 때까지 더 많은 마커를 추가할 수 있습니다. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* DO NOT EXCEED THIS VALUE. */
	/* 이 값을 초과하지 마십시오. */
	VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#include <hash.h>
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;

#define VM_TYPE(type) ((type) & 7)

/* The representation of "page".
 * This is kind of "parent class", which has four "child class"es, which are
 * uninit_page, file_page, anon_page, and page cache (project4).
 * DO NOT REMOVE/MODIFY PREDEFINED MEMBER OF THIS STRUCTURE. */
/* "페이지"의 표현입니다.
 * 이는 "부모 클래스"의 일종으로, uninit_page, file_page, anon_page, 및 page cache(project4)라는 네 개의 "자식 클래스"를 가집니다.
 * 이 구조체의 사전 정의된 멤버를 제거/수정하지 마십시오. */
struct page {
	const struct page_operations *operations;
	void *va;              /* Address in terms of user space */
	                      /* 사용자 공간의 주소 */
	struct frame *frame;   /* Back reference for frame */
	                      /* 프레임에 대한 역참조 */

	/* Your implementation */
	/* 사용자의 구현 */
	struct hash_elem h_elem;
	bool writable;

	/* Per-type data are binded into the union.
	 * Each function automatically detects the current union */
	/* 유형별 데이터는 유니언에 바인딩됩니다.
	 * 각 함수는 현재 유니언을 자동으로 감지합니다. */
	union {
		struct uninit_page uninit;
		struct anon_page anon;
		struct file_page file;
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	};
};

/* The representation of "frame" */
/* "프레임"의 표현 */
struct frame {
	void *kva;          /* 커널 가상 주소 */
	struct page *page;  /* 페이지 참조 */
	struct list_elem f_elem;  /* 리스트 요소 */
};

/* The function table for page operations.
 * This is one way of implementing "interface" in C.
 * Put the table of "method" into the struct's member, and
 * call it whenever you needed. */
/* 페이지 작업에 대한 함수 테이블.
 * 이는 C에서 "인터페이스"를 구현하는 한 가지 방법입니다.
 * "메서드"의 테이블을 구조체의 멤버에 넣고 필요할 때마다 호출하십시오. */
struct page_operations {
	bool (*swap_in) (struct page *, void *);
	bool (*swap_out) (struct page *);
	void (*destroy) (struct page *);
	enum vm_type type;
};

#define swap_in(page, v) (page)->operations->swap_in ((page), v)
#define swap_out(page) (page)->operations->swap_out (page)
#define destroy(page) \
	if ((page)->operations->destroy) (page)->operations->destroy (page)

/* Representation of current process's memory space.
 * We don't want to force you to obey any specific design for this struct.
 * All designs up to you for this. */
/* 현재 프로세스의 메모리 공간 표현.
 * 이 구조체에 대해 특정 디자인을 따르도록 강요하고 싶지 않습니다.
 * 모든 디자인은 여러분에게 달려 있습니다. */
struct supplemental_page_table {
	struct hash hash_table;  /* 해시 테이블 */
};

struct swap_table {
	struct hash swap_table;  /* 스왑 테이블 */
};

#include "threads/thread.h"
void supplemental_page_table_init (struct supplemental_page_table *spt);
bool supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src);
void supplemental_page_table_kill (struct supplemental_page_table *spt);
struct page *spt_find_page (struct supplemental_page_table *spt,
		void *va);
bool spt_insert_page (struct supplemental_page_table *spt, struct page *page);
void spt_remove_page (struct supplemental_page_table *spt, struct page *page);

void vm_init (void);
bool vm_try_handle_fault (struct intr_frame *f, void *addr, bool user,
		bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
	vm_alloc_page_with_initializer ((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage,
		bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page (struct page *page);
bool vm_claim_page (void *va);
enum vm_type page_get_type (struct page *page);



#define MAX_STACK_BOTTOM	USER_STACK - 0x100000

uint64_t hash_hash_func_impl(const struct hash_elem *e, void *aux);
bool hash_less_func_impl (const struct hash_elem *a_, const struct hash_elem *b_, void *aux);


struct lazy_load_info {
	struct file *file;
	size_t ofs;
	size_t read_bytes;
	size_t zero_bytes;
};


struct list frame_table;
struct lock frame_lock;
#endif  /* VM_VM_H */
