savedcmd_synapse_kmod.mod := printf '%s\n'   src/synapse_main.o src/synapse_sysfs.o src/synapse_probe.o src/synapse_sched.o src/synapse_ctx.o | awk '!x[$$0]++ { print("./"$$0) }' > synapse_kmod.mod
