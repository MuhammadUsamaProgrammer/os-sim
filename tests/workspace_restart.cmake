# workspace_restart.cmake - Modules menu persistence across real application restarts.
# Runs the GUI binary (offscreen) several times with a private XDG_CONFIG_HOME, so the user's own
# settings are never read or written:
#   1. save a customised workspace, 2. restart and read it back, 3. corrupted values,
#   4. an unsupported settings version, 5. a file that is not an INI file at all.
# Usage: cmake -DOSSIM=<ossim> -DCFG=<dir> -P workspace_restart.cmake
file(REMOVE_RECURSE "${CFG}")
file(MAKE_DIRECTORY "${CFG}")
set(ENV{XDG_CONFIG_HOME} "${CFG}")
set(ENV{QT_QPA_PLATFORM} "offscreen")
set(conf "${CFG}/OS-Sim/OS-Sim.conf")

function(run expect_rc out_var)
  execute_process(COMMAND "${OSSIM}" ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
  if(NOT rc EQUAL expect_rc)
    message(FATAL_ERROR "ossim ${ARGN}: exit ${rc} (expected ${expect_rc})\n${out}\n${err}")
  endif()
  set(${out_var} "${out}" PARENT_SCOPE)
endfunction()

function(expect text what)
  string(FIND "${text}" "${what}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "expected '${what}' in:\n${text}")
  endif()
endfunction()

# 1. customise and save (one refused change: the last visible child of Storage & Files)
run(2 out --workspace-set "memory.heap=0,processes=0,processes.monitor=0,storage.explorer=0,storage.access=0,storage.blocks=0,storage.recovery=0")
expect("${out}" "refused storage.recovery=0")
# 2. restart: the workspace comes back, including child choices of the hidden parent
run(0 out --workspace-report)
expect("${out}" "[ ] Heap Laboratory")
expect("${out}" "[ ] Processes (2/3 shown)")
expect("${out}" "[x] Process Lifecycle (parent hidden)")
expect("${out}" "[ ] System Monitor")
expect("${out}" "[x] Storage & Files (1/4 shown)")
expect("${out}" "[x] Recovery Lab")
expect("${out}" "[x] Deadlock")
# other settings in the same file are preserved
file(READ "${conf}" c)
file(WRITE "${conf}" "${c}\n[layout]\nkeepme=42\n")
run(0 out --workspace-set "deadlock=0")
file(READ "${conf}" c)
expect("${c}" "keepme=42")
# 3. corrupted values and unknown / removed ids: defaults for the bad entries, no crash, rules kept
file(WRITE "${conf}" "[modules]\nversion=1\nv1\\visible\\memory=banana\nv1\\visible\\removed.module=false\nv1\\visible\\scheduling=false\nv1\\visible\\processes=false\nv1\\visible\\memory.heap=false\nv1\\visible\\storage.explorer=false\nv1\\visible\\storage.access=false\nv1\\visible\\storage.blocks=false\nv1\\visible\\storage.recovery=false\nv1\\presets\\size=1\nv1\\presets\\1\\name=Broken\nv1\\presets\\1\\state=memory=1,,junk,disk=7\n")
run(0 out --workspace-report)
expect("${out}" "bad value 'banana' for memory")
expect("${out}" "[x] Memory (7/8 shown)")
expect("${out}" "[ ] Scheduling")
expect("${out}" "[x] Storage & Files (1/4 shown)")
expect("${out}" "preset 'Broken': 2 malformed entries skipped")
# every main module hidden in the file: one is shown again
file(WRITE "${conf}" "[modules]\nversion=1\nv1\\visible\\scheduling=false\nv1\\visible\\processes=false\nv1\\visible\\memory=false\nv1\\visible\\deadlock=false\nv1\\visible\\synchronization=false\nv1\\visible\\threads=false\nv1\\visible\\ipc=false\nv1\\visible\\io=false\nv1\\visible\\disk=false\nv1\\visible\\storage=false\n")
run(0 out --workspace-report)
expect("${out}" "[x] Scheduling (3/3 shown)")
# 4. unsupported version: defaults
file(WRITE "${conf}" "[modules]\nversion=99\nv1\\visible\\memory=false\n")
run(0 out --workspace-report)
expect("${out}" "version '99' is not supported")
expect("${out}" "[x] Memory (8/8 shown)")
# 5. not an INI file at all
string(ASCII 1 2 255 junk)
file(WRITE "${conf}" "${junk} this is not a settings file [[[ \n=== \n")
run(0 out --workspace-report)
expect("${out}" "[x] Memory (8/8 shown)")
file(REMOVE_RECURSE "${CFG}")
message(STATUS "workspace restart test passed")
