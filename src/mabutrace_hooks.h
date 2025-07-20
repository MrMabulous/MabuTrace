#ifndef __MABUTRACE_HOOKS_H__
#define __MABUTRACE_HOOKS_H__

#ifdef __cplusplus
extern "C" {
#endif


#ifndef __ASSEMBLER__
void trace_task_switch(unsigned char type);

// This macro is called when a task is about to be switched out.
#define traceTASK_SWITCHED_OUT() \
  do { \
    trace_task_switch(7); \
  } while(0)

// This macro is called when a task has just been switched in.
#define traceTASK_SWITCHED_IN() \
  do { \
    trace_task_switch(6); \
  } while(0)

#endif

#ifdef __cplusplus
}
#endif

#endif // FREERTOS_TRACE_HOOKS_H