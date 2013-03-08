A transaction based Recoverable Virtual Memory library in C.
It maps a memory segment on a disk and provides transactional capabilities to main memory interactions.

Files of interest - rvm.c

To use the rvm library:

1. Execute "make clean"
2. Execute "make"
3. Include the "rvm.h" header file in your program.
4. Make sure you have write privilage on the current directory.
5. Use the functions defined in the header file as appropriate.
6. To use the library file, compile the program using - "gcc -g -static <file_name>.c -L. -lrvm -o <file_name>"
