#!/bin/bash

ENTRYPOINT_PREFIX="mx"
SYSCALL_MAP=""
# Fetch info from lib sys system call implementation files
# regarding towards which entry-point they lead to.

examine_syscalls_impl()
{

	MXLIBC_PATH="$MINIX_ROOT/minix/minix/lib/libc/sys"
	cd "$MXLIBC_PATH"
	SYSCALL_MAP=$( for e in `(cd $MXLIBC_PATH && git grep "PROC_NR" | cut -d : -f 1 | sort | uniq )`
	do
		value="`grep "PROC_NR" $e | grep -o "[^(]*," | sed 's/,$//g' | tr "\n" "?" | sed 's/?/; /g' | sed 's/; $//g'`"
		# open.c has a branched condition, for which we make some special adjustments
		if [[ "$value" == "VFS_PROC_NR, call" ]]
		then
			value1=`echo $value | sed 's/call/VFS_CREAT/g'`
			value2=`echo $value | sed 's/call/VFS_OPEN/g'`
			value="`printf "%s; %s" "$value1" "$value2"`"
		fi
		printf "%s = %s\n" "$e" "$value"
	done )


	#export SYSCALLMAP="$SYSCALL_MAP"
}

# Get the entry point from whatever info we have right now.
get_entrypoints()
{
	(
	cd "$MINIX_ROOT/minix/minix"
	for e in `echo "${SYSCALL_MAP}" | cut -d= -f2 | tr ";" "\n" | cut -d, -f2`
	do
		mod_name=`echo $e | cut -d_ -f1 | tr "[:upper:]" "[:lower:]"`
		entrypoint=`git grep -w $e | grep -wo do_[a-zA-Z_0-9]*`
		# printf "map_entry: %s\tmod_name:%s\tentrypoint:%s\n" "$e" "${mod_name}" "${entrypoint}"
		printf "%s_%s_%s\n" "${ENTRYPOINT_PREFIX}" "${mod_name}" "${entrypoint}"
	done
	)

}

examine_syscalls_impl
get_entrypoints | sort | uniq
