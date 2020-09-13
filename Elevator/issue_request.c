#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/syscalls.h>
/* System call stub */

long (*STUB_issue_request)(int,int,int) = NULL;
EXPORT_SYMBOL(STUB_issue_request);

/* System call wrapper */
SYSCALL_DEFINE3(issue_request, int, temp1, int, temp2, int, temp3) {
        printk(KERN_NOTICE "Inside SYSCALL_DEFINE1 block");
if (STUB_issue_request != NULL)
        return STUB_issue_request(temp1,temp2,temp3);
else
return -ENOSYS;
}

