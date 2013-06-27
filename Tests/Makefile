all: test.c Crasher.c Counter.c
	gcc  -g -O0 -o test test.c
	#gcc  -g -pthread -O0 -o Crasher Crasher.c
	gcc  -g -pthread -O0 -o Counter Counter.c

clean:
	-rm test
