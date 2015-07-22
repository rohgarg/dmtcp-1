default: run-tests

build-tests:
	gcc -g -Wall -I./minunit -c get_restart_env_test.c
	gcc -g -Wall -I./minunit -c get_restart_env.c
	gcc -g -Wall -I./minunit -c readLine.c
	gcc -g -Wall -I./minunit -c readLineTest.c
	gcc -g -Wall get_restart_env_test.o get_restart_env.o readLine.o -o get_restart_env_test
	gcc -g -Wall readLineTest.o readLine.o -o readLineTest

run-tests: build-tests
	./get_restart_env_test
	./readLineTest

clean:
	rm -f *.o
	rm -f get_restart_env_test
