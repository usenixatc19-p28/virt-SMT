#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <asm/uaccess.h>

#define PROCFS_NAME 		"myprocfile"

/**
 * This structure holds information about the /proc file
 *
 */
static struct proc_dir_entry *Our_Proc_File;


/** 
 * This function is called then the /proc file is read
 * It copies the string from procfs_buffer to buffer (the first parameter).
 */
int procfile_read(char *buffer,
	      char **buffer_location,
	      off_t offset, int buffer_length, int *eof, void *data)
{
	int ret;
	for(ret=0;ret<81920;ret++) {
		if(need_resched()) break;
		cpu_relax();
	}
	return ret;
}

int start_module(void)
{
	/* create the /proc file */
	Our_Proc_File = create_proc_entry(PROCFS_NAME, 0644, NULL);
	
	if (Our_Proc_File == NULL) {
		remove_proc_entry(PROCFS_NAME, NULL);
		printk(KERN_ALERT "Error: Could not initialize /proc/%s\n",
			PROCFS_NAME);
		return -ENOMEM;
	}

	Our_Proc_File->read_proc  = procfile_read;
	Our_Proc_File->mode 	  = S_IFREG | S_IRUGO;
	Our_Proc_File->uid 	  = 0;
	Our_Proc_File->gid 	  = 0;

	printk(KERN_INFO "/proc/%s created\n", PROCFS_NAME);	
	return 0;	/* everything is ok */
}

/**
 *This function is called when the module is unloaded
 *
 */
void stop_module(void)
{
	remove_proc_entry(PROCFS_NAME, NULL);
	printk(KERN_INFO "/proc/%s removed\n", PROCFS_NAME);
}

module_init(start_module);
module_exit(stop_module);
MODULE_LICENSE("GPL");
