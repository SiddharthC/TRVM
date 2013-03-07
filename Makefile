all:
  @gcc -g -c rvm.c -o rvm.o
	@ar rcs librvm.a rvm.o

clean:
	@rm -f rvm.o librvm.a
