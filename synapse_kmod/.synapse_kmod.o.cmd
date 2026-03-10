savedcmd_synapse_kmod.o := ld -m elf_x86_64 -z noexecstack --no-warn-rwx-segments   -r -o synapse_kmod.o @synapse_kmod.mod  ; /usr/lib/modules/6.19.6-arch1-1/build/tools/objtool/objtool --hacks=jump_label --hacks=noinstr --hacks=skylake --ibt --orc --retpoline --rethunk --sls --static-call --uaccess --prefix=16  --link  --module synapse_kmod.o

synapse_kmod.o: $(wildcard /usr/lib/modules/6.19.6-arch1-1/build/tools/objtool/objtool)
