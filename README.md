## nbudstee: Non-Blocking Unix Domain Socket Tee

**Tees Input to zero or more non-blocking Unix domain sockets, each of which can have zero or more connected readers,**  
**and/or to zero or more existing FIFOs, each of which can have exactly one existing reader.**  
**Also copies to STDOUT, unless -n/--no-stdout is used.**  
**Input defaults to STDIN**  

### Usage:

    nbudstee [options] uds1 uds2 ...

Where uds1 uds2 ... are zero or more filenames to use as Unix domain sockets, or existing FIFOs.

### Options:
* -n, --no-stdout  
  Do not copy input to STDOUT.  
* -b, --unlink-before  
  First try to unlink any existing sockets. This will not try to unlink non-sockets.  
* -u, --unlink-after  
  Try to unlink all sockets and FIFOs when done.  
* -m, --max-queue bytes  
  Maximum amount of data to buffer for each connected reader (approximate).  
  Accepts suffixes: k, M, G, for kilobytes, megabytes, gigabytes (multiples of 1024).  
  Default: 64k.  
  Above this limit new data for that socket reader will be discarded.  
* -i, --input file  
  Read from file instead of STDIN.  
* -I, --input-reopen file  
  Read from file instead of STDIN.  
  When the end of input is reached, reopen from the beginning.  
  This is primarily intended for FIFOs.  

### Use Case:
On demand logging, in particular in shell pipelines.

    ... | process | nbudstee -u uds1 | process | ...
    ... | process 2> >(nbudstee -n -u uds2) | ...

Any number of readers can read from each socket, on demand, without blocking the main pipeline, or requiring data to be dumped to a file.  
This is useful for occasional debugging/sampling, or for inserting/removing tees without interrupting the main pipeline.

A possible command to read from a Unix domain socket is:

    nc -U uds1

### Notes:
* No attempt is made to line-buffer or coalesce the input.
  A reader could receive a partial line when connecting/disconnecting if the input source is not line-buffered.  
  If required line-buffer the input before piping it to nbudstee.
* Writes to STDOUT are blocking.


### URLs:
* This project is hosted at https://github.com/JGRennison/nbudstee
* A Ubuntu PPA is currently available at https://launchpad.net/~j-g-rennison/+archive/nbudstee

### License:
GPLv2
