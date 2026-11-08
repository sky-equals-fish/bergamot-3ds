// SPDX-License-Identifier: GPL-3.0-or-later
// macOS test interposer: fail on executable allocations/protection changes.
// This is a diagnostic denial guard, not an executable-memory workaround.
#include <sys/mman.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <unistd.h>
#include <cstdlib>
namespace {
[[noreturn]] void reject(){constexpr char message[]="BERGAMOT_TEST: executable memory rejected\n";write(STDERR_FILENO,message,sizeof(message)-1);_Exit(86);}
void* checked_mmap(void* address,size_t size,int protection,int flags,int fd,off_t offset){
 if(protection&PROT_EXEC)reject();
#ifdef MAP_JIT
 if(flags&MAP_JIT)reject();
#endif
 return mmap(address,size,protection,flags,fd,offset);
}
int checked_mprotect(void* address,size_t size,int protection){if(protection&PROT_EXEC)reject();return mprotect(address,size,protection);}
kern_return_t checked_mach_vm_protect(vm_map_t task,mach_vm_address_t address,mach_vm_size_t size,boolean_t maximum,vm_prot_t protection){if(protection&VM_PROT_EXECUTE)reject();return mach_vm_protect(task,address,size,maximum,protection);}
#define INTERPOSE(replacement,original) __attribute__((used)) static struct {const void* replace;const void* original;} interpose_##original __attribute__((section("__DATA,__interpose")))={(const void*)&replacement,(const void*)&original}
INTERPOSE(checked_mmap,mmap);
INTERPOSE(checked_mprotect,mprotect);
INTERPOSE(checked_mach_vm_protect,mach_vm_protect);
}
