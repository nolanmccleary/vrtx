# gdb script for the QEMU target.  Usage (after ./qemu/run.sh in another shell):
#   arm-none-eabi-gdb build/test.elf -x qemu/debug.gdb
#
# Stops the instant the firmware traps, with a live C backtrace + locals -- the
# thing JTAG on the real board can't give you. Every corruption path funnels
# through raise_error_ctx (the EDF watchdog + next_up traps) or fault_capture
# (any CPU abort), so breaking on both catches the bug with full context.

target remote :1234

break raise_error_ctx
break raise_error
break fault_capture

# Dump the harness watchdog report (set by edf_flag_corruption before it traps).
define wd
  printf "thread=0x%08x core=%d task=%d expected=0x%08x actual=0x%08x tick=%d\n", \
    g_edf_bad_thread, g_edf_bad_core, g_edf_bad_task, \
    g_edf_bad_expected, g_edf_bad_actual, g_edf_bad_tick
end
document wd
  Print the EDF watchdog corruption report (thread/core/task/expected/actual/tick).
end

# Watch a thread_t's status byte to catch WHOEVER writes it. thread_status sits at
# offset 0x2015 (stack[0x2000] + fields). Pass the thread_t* address:
#   wpstatus 0x60014...
define wpstatus
  watch *(unsigned char *)($arg0 + 0x2015)
end
document wpstatus
  Hardware-watch a thread_t's thread_status byte. Arg: the thread_t* address.
  gdb halts on the write, so `backtrace` names the exact code that set it.
end

printf "\n[vrtx-qemu] connected. 'continue' to run; on a trap use 'wd' then 'backtrace'.\n"
printf "[vrtx-qemu] to catch the writer: get a thread addr, then 'wpstatus <addr>'.\n\n"
