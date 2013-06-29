all: test.c Crasher.c Counter.c
	gcc  -static -g -O0 -o test test.c
	#gcc  -static -g -pthread -O0 -o Crasher Crasher.c
	gcc  -static -g -pthread -O0 -o Counter Counter.c
	gcc  -static -g -pthread -O0 -o granularity_test granularity_test.c

clean:
	-rm test
