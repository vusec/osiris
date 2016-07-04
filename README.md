OSIRIS
======

Supported systems
-----------------

OSIRIS can be built on Linux and installed either on a virtual machine or
run directly on bare hardware. Our scripts automatically create an OSIRIS VM
to perform experiments with. Our prototype is based on MINIX3 and
after building the system, it be installed in the same way and
on the same systems as MINIX3. For more information, see the
[MINIX3 user guide](http://wiki.minix3.org/doku.php?id=usersguide:start).

On the build machine, our system requires approximately 35GB of disk space
when fully built. This includes dependencies fetched by the automatic
build script. The system also requires a functional internet connection
to be able to download required packages. We recommend building OSIRIS
on a machine with at least 4GB of RAM but preferably more as link-time
optimizations cause the linker to use a considerable amount of memory.

Prerequisites
-------------

While our automatic setup script downloads and builds some of the most important
dependencies, our system does require some packages to be present on the machine
used to build it. We have tested the OSIRIS build on ubuntu-15.10-server-amd64
and found that it requires the following packages to be installed before
the automatic build script is invoked:

    sudo apt-get install bison curl flex install g++ gcc gettext git make \
                         pkg-config python ssh subversion zlib1g-dev

It should be noted that our system cannot be compiled with 5.x versions of GCC
because these versions cannot properly compile LLVM plugins.
A 4.x version of GCC must be installed to be able to build OSIRIS.

Building OSIRIS
---------------

After ensuring that all prerequisites are installed, download the OSIRIS
source repository as follows:

    git clone https://github.com/vusec/osiris.git

Next, invoke the automatic build script:

    cd osiris
    ./autosetup-minix.sh

This scripts downloads, builds and installs the remaining dependencies locally
(in the autosetup.dir subdirectory), downloads and builds the OSIRIS/MINIX
source tree and creates a virtual machine image to conduct experiments with.

Running OSIRIS
--------------

To simply run the OSIRIS virtual machine that was just created, execute
the following commands from the root directory of the git repository:

    cd apps/minix
    ./clientctl run

To perform fault injection experiments, use the prun-scripts/edfi-ltckpt.sh
script from the apps/minix directory. This script performs the following steps:

* recompiles OSIRIS with the specified recovery model
* creates a new virtual machine with the recompiled OSIRIS
* the first run serves as a golden run with no faults injected
* on subsequent runs the script selects faults (using the results
  from the golden run to ensure they are actually triggered) and injects them
* finally, the script starts the virtual machine and executes the MINIX
  test suite as a workload to test the system

There are many environment variables that can be used to configure the script.
These are the most important ones:

* EDFIFAILSTOP:  zero injects all EDFI fault types, non-zero injects only
                 fail-stop faults
* RECOVERYMODEL: 0 disables recovery,
                 4 selects the pessimistic recovery model,
                 5 selects the naive recovery model,
                 8 selects stateless recovery,
                 9 selects the enhanced recovery model
* PRUNITER:      number of experiments to perform

Execution logs are stored in the results/minix directory under
the root directory of the git repository. To analyse these logs,
use the minixtestloganalyze tool in
llvm/tools/hypermemloganalyze/minixtestloganalyze. For example,
execute commands like this from the root directory of the git repository:

    make -C llvm/tools/hypermemloganalyze/minixtestloganalyze
    llvm/tools/hypermemloganalyze/minixtestloganalyze/minixtestloganalyze   \
      results/minix/faulty-ltckpt-all-rm4-20160617-132330/hypermemlog-*.txt \
      results/minix/faulty-ltckpt-all-rm4-20160617-132330/maps/*.map

This performs an analysis of a run with EDFIFAILSTOP=0 and RECOVERYMODEL=4
that was executed at 2016-06-17 13:23:30 (check the results/minix directory
to find the timestamps for your own logs and replace it in the command above).
The minixtestloganalyze tool has many options to provide additional information,
which it lists if the -h command line argument is specified.

Service Disruption Experiment
-----------------------------

Series of deterministic faults are injected into Process Manager and Unixbench test set is 
run to demonstrate continuity of services in OSIRIS despite of constant onset of faults in 
core OS components. This can be run by using the following mentioned script

	cd apps/minix/scripts
	MROOT=<path to apps/minix directory> ./osiris_demo.sh 

The script supports several options viz.,
 
	prepare - Builds the OS and sets up the demo
	run 	- Initiates demo
	graph   - Plots RS activity using gnuplot

Recovery Coverage Experiment
----------------------------

Recovery coverage is obtained by tracking num of instructions executed within recovery windows 
in the components of OSIRIS during execution. Workload used is the MINIX test set.

	cd apps/minix/scripts
	MROOT=<path to apps/minix directory> ./drec_rwindow_profiling.sh 

After completion, serial output is saved in apps/minix/minix/llvm/serial.out.
Result aggregation can be done using the following:

	cd apps/minix/scripts
	./drec_fetch_rwindow_profiling_numbers.sh <path to the serial.out file>

Performance Evaluation
----------------------

perf-\*.sh files in apps/minix/scripts directory are for running Unixbench performance 
experiments. Following are the mappings:

*	baseline - This is the bare MINIX 3 
*	ltckptrecovery - refers to OSIRIS checkpoint-recovery instrumentations
*	bbclone - One with recovery window aware checkpointing optimization
