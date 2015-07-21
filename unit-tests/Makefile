default: run-tests

build-tests:
	gcc -Wall -I./minunit -c get_restart_env_test.c
	gcc -Wall -I./minunit -c get_restart_env.c
	gcc -g -Wall get_restart_env_test.o get_restart_env.o -o get_restart_env_test

run-tests: build-tests
	./get_restart_env_test

clean:
	rm -f *.o
	rm -f get_restart_env_test
