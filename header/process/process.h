#ifndef _PROCESS_H
#define _PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpu/interrupt.h"
#include "fs/ext2.h"
#include "memory/paging.h"
#include "process/process_info.h"

// 256 code/data frames (1 MiB, matching linker.ld's user program size cap) + 1 stack frame
#define PROCESS_PAGE_FRAME_COUNT_MAX 257
#define PROCESS_PRIORITY_DEFAULT 1u
#define PROCESS_PRIORITY_MAX 255u

/// Kernel stack reserved for each process, used on interrupts from ring 3.
#define PROCESS_KERNEL_STACK_SIZE 0x1000u

/**
 * Raw interrupt-frame snapshot size (in dwords), measured from the ISR's
 * frame base (the PUSHAD image) through the inter-privilege SS/ESP slots:
 *
 *   0 edi, 1 esi, 2 ebp, 3 esp-slot, 4 ebx, 5 edx, 6 ecx, 7 eax,
 *   8 gs,  9 fs,  10 es, 11 ds, 12 int_number, 13 error_code,
 *   14 eip, 15 cs, 16 eflags, 17 user esp, 18 user ss
 *
 * Restoring is the exact inverse of the ISR epilogue: `popad`, four segment
 * pops, `add esp, 8`, then `iret` (which consumes eip/cs/eflags/esp/ss).
 */
#define CONTEXT_INTERRUPT_FRAME_DWORD_COUNT 19

/**
 * struct InterruptFrame covers the frame up to EFLAGS. The inter-privilege
 * ESP/SS slots live two dwords past it and are only present for ring-3
 * interrupts, so the raw snapshot includes two extra dwords.
 */
_Static_assert(
    sizeof(struct InterruptFrame) ==
        (CONTEXT_INTERRUPT_FRAME_DWORD_COUNT - 2) * sizeof(uint32_t),
    "InterruptFrame layout no longer matches the saved raw interrupt frame"
);

enum ContextInterruptFrameIndex {
    CONTEXT_FRAME_EDI = 0,
    CONTEXT_FRAME_ESI,
    CONTEXT_FRAME_EBP,
    CONTEXT_FRAME_ESP_SLOT,
    CONTEXT_FRAME_EBX,
    CONTEXT_FRAME_EDX,
    CONTEXT_FRAME_ECX,
    CONTEXT_FRAME_EAX,
    CONTEXT_FRAME_GS,
    CONTEXT_FRAME_FS,
    CONTEXT_FRAME_ES,
    CONTEXT_FRAME_DS,
    CONTEXT_FRAME_INT_NUMBER,
    CONTEXT_FRAME_ERROR_CODE,
    CONTEXT_FRAME_EIP,
    CONTEXT_FRAME_CS,
    CONTEXT_FRAME_EFLAGS,
    CONTEXT_FRAME_USER_ESP,
    CONTEXT_FRAME_USER_SS,
};

#define KERNEL_RESERVED_PAGE_FRAME_COUNT 4
#define KERNEL_VIRTUAL_ADDRESS_BASE 0xC0000000

#define CPU_EFLAGS_BASE_FLAG 0x2
#define CPU_EFLAGS_FLAG_CARRY 0x1
#define CPU_EFLAGS_FLAG_PARITY 0x4
#define CPU_EFLAGS_FLAG_AUX_CARRY 0x10
#define CPU_EFLAGS_FLAG_ZERO 0x40
#define CPU_EFLAGS_FLAG_SIGN 0x80
#define CPU_EFLAGS_FLAG_TRAP 0x100
#define CPU_EFLAGS_FLAG_INTERRUPT_ENABLE 0x200
#define CPU_EFLAGS_FLAG_DIRECTION 0x400
#define CPU_EFLAGS_FLAG_OVERFLOW 0x800
#define CPU_EFLAGS_FLAG_IO_PRIVILEGE 0x3000
#define CPU_EFLAGS_FLAG_NESTED_TASK 0x4000
#define CPU_EFLAGS_FLAG_MODE 0x8000
#define CPU_EFLAGS_FLAG_RESUME 0x10000
#define CPU_EFLAGS_FLAG_VIRTUAL_8086 0x20000
#define CPU_EFLAGS_FLAG_ALIGNMENT_CHECK 0x40000
#define CPU_EFLAGS_FLAG_VINTERRUPT_FLAG 0x80000
#define CPU_EFLAGS_FLAG_VINTERRUPT_PENDING 0x100000
#define CPU_EFLAGS_FLAG_CPUID_INSTRUCTION 0x200000
#define CPU_EFLAGS_FLAG_AES_SCHEDULE_LOAD 0x40000000
#define CPU_EFLAGS_FLAG_ALTER_INSTRUCTION 0x80000000

// Return code constant for process_create_user_process()
#define PROCESS_CREATE_SUCCESS 0
#define PROCESS_CREATE_FAIL_MAX_PROCESS_EXCEEDED 1
#define PROCESS_CREATE_FAIL_INVALID_ENTRYPOINT 2
#define PROCESS_CREATE_FAIL_NOT_ENOUGH_MEMORY 3
#define PROCESS_CREATE_FAIL_FS_READ_FAILURE 4
#define PROCESS_CREATE_FAIL_INVALID_NAME 5

/**
 * One frame is reserved for the user stack, so PROCESS_PAGE_FRAME_COUNT_MAX
 * must stay at least one larger than the largest program page count. The
 * default 1 MiB program cap therefore needs 257 frames.
 */
/**
 * Contain information needed for task to be able to get interrupted and resumed later
 *
 * @param cpu                         All CPU register state
 * @param eip                         CPU instruction counter to resume execution
 * @param eflags                      Flag register to load before resuming the execution
 * @param page_directory_virtual_addr CPU register CR3, containing pointer to active page directory
 */
struct Context {
    struct CPURegister cpu;
    uint32_t eip;
    uint32_t eflags;
    uint32_t page_directory_virtual_addr;

    /** Raw snapshot of the interrupt frame, used for context switching. */
    uint32_t interrupt_frame[CONTEXT_INTERRUPT_FRAME_DWORD_COUNT];

    /** Top (exclusive) of this process's own kernel stack. */
    uint32_t kernel_stack_top;
};

/**
 * Structure data containing information about a process
 *
 * @param metadata Process metadata, contain various information about process
 * @param context  Process context used for context saving & switching
 * @param memory   Memory used for the process
 */
struct ProcessControlBlock {
    struct {
        uint32_t pid;
        char name[PROCESS_NAME_LENGTH_MAX];
        enum PROCESS_STATE state;

        /** Higher value means the scheduler runs this process first. */
        uint8_t priority;
    } metadata;

    struct Context context;

    struct {
        void *virtual_addr_used[PROCESS_PAGE_FRAME_COUNT_MAX];
        uint32_t page_frame_used_count;
    } memory;
};

struct ProcessManagerState {
    bool is_process_active[PROCESS_COUNT_MAX];
    uint32_t active_process_count;
};

extern struct ProcessControlBlock _process_list[PROCESS_COUNT_MAX];

extern struct ProcessManagerState process_manager_state;

/**
 * Get currently running process PCB pointer
 * 
 * @return Will return NULL if there's no running process
 */
struct ProcessControlBlock *process_get_current_running_pcb_pointer(void);

/**
 * Build the initial interrupt frame for a freshly created process so the
 * first context restore can `iret` straight into ring 3.
 *
 * @param context                    Context to initialize
 * @param entrypoint                 User program entrypoint (typically 0)
 * @param page_directory_virtual_addr Page directory virtual address for CR3
 * @param kernel_stack_top           Top of this process's kernel stack
 */
void context_init(
    struct Context *context,
    uint32_t entrypoint,
    uint32_t page_directory_virtual_addr,
    uint32_t kernel_stack_top
);

/**
 * True when the most recent interrupt carried a valid ring-3 frame that was
 * successfully saved into the running process. The scheduler only switches
 * tasks when this is set - intra-privilege interrupts have no ring-3 frame.
 */
extern volatile bool process_interrupt_frame_valid;

/**
 * Snapshot the live interrupt frame into the currently running process.
 * Called from the ISR while `esp` points at the frame base.
 *
 * @param interrupt_frame Pointer to the ISR's raw frame base
 * @param from_ring3      Whether the interrupt came from user mode
 */
void process_save_current_context(
    const void *interrupt_frame,
    bool from_ring3
);

/**
 * Restore a saved context and jump into ring 3. Implemented in asm; never
 * returns. Loads the frame onto the target kernel stack, switches CR3 and
 * the TSS kernel stack, then restores registers and `iret`s.
 *
 * @param kernel_stack_top Top of the target process's kernel stack
 * @param interrupt_frame  Raw frame snapshot to restore
 */
void context_restore(
    uint32_t kernel_stack_top,
    const uint32_t *interrupt_frame
);

/**
 * Create new user process and setup the virtual address space.
 * All available return code is defined with macro "PROCESS_CREATE_*"
 * 
 * @note          This procedure assumes no reentrancy in ISR
 * @param request  Appropriate read request for the executable
 * @param priority Scheduling priority; 0 becomes PROCESS_PRIORITY_DEFAULT
 * @param out_pid  Optional destination for the PID of the new process
 * @return         Process creation return code
 */
int32_t process_create_user_process(
    struct EXT2DriverRequest request,
    uint8_t priority,
    uint32_t *out_pid
);

/**
 * Replace the running process's image with `request`, preserving its PID,
 * priority and kernel stack. On success this never returns - it iret's into
 * the new image. On failure the old image is left untouched and an error code
 * is returned.
 */
int32_t process_exec_current(struct EXT2DriverRequest request);

/**
 * Copy metadata for active processes into `buffer` in PCB-index order.
 *
 * @return Number of processes written.
 */
uint32_t process_list_snapshot(
    struct ProcessInfo *buffer,
    uint32_t capacity
);

/**
 * Destroy process then release page directory and process control block
 * 
 * @param pid Process ID to delete
 * @return    True if process destruction success
 */
bool process_destroy(uint32_t pid);

#endif
