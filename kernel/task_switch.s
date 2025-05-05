# task_switch.s - Task context switching implementation

.section .text
.global task_context_switch
.global task_context_start

# void task_context_switch(task_context_t* old_context, task_context_t* new_context);
# Function to switch from one task to another
task_context_switch:
    # Save parameters
    movl 4(%esp), %eax  # old_context
    movl 8(%esp), %edx  # new_context

    # Save current register state to old context
    pushl %ebp
    movl %esp, %ebp
    
    # Save non-volatile registers to old context
    movl %edi, 0(%eax)   # old_context->edi
    movl %esi, 4(%eax)   # old_context->esi
    movl %ebx, 8(%eax)   # old_context->ebx
    movl %ebp, 12(%eax)  # old_context->ebp
    
    # Save return address as EIP in old context
    movl (%ebp), %ecx    # Get saved ebp
    movl 4(%ebp), %ecx   # Get return address
    movl %ecx, 16(%eax)  # old_context->eip

    # Load new context registers
    movl 0(%edx), %edi   # new_context->edi
    movl 4(%edx), %esi   # new_context->esi
    movl 8(%edx), %ebx   # new_context->ebx
    movl 12(%edx), %ebp  # new_context->ebp
    
    # Prepare return address from new context's EIP
    pushl 16(%edx)       # new_context->eip
    
    # Restore stack and return to new task
    ret

# void task_context_start(task_context_t* context);
# Function to start a new task for the first time
task_context_start:
    # Get parameter
    movl 4(%esp), %edx  # context
    
    # Load context registers
    movl 0(%edx), %edi   # context->edi
    movl 4(%edx), %esi   # context->esi
    movl 8(%edx), %ebx   # context->ebx
    movl 12(%edx), %ebp  # context->ebp
    
    # Jump to task entry point (EIP)
    jmp *16(%edx)        # context->eip