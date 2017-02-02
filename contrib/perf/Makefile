# To demonstrate, do:  make check    [Checkpoints every 5 seconds]

# The name will be the same as the current directory name.
NAME=${shell basename $$PWD}

# By default, your resulting library will have this name.
LIBNAME=dmtcp_${NAME}hijack

# As you add new files to your hijack library, add the object file names here.
LIBOBJS = ${NAME}.o

# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
   DMTCP_ROOT=../../
endif
DMTCP_INCLUDE=${DMTCP_ROOT}/include
JALIB_INCLUDE=${DMTCP_ROOT}/jalib

#CFLAGS += -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} -fPIC -c --std=gnu99 -O0 -g -DMYPLUG_DEBUG
#CXXFLAGS += -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} -fPIC -c -O0 -g -DMYPLUG_DEBUG
CFLAGS += -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} -fPIC -c --std=gnu99 -O0 -g3
CXXFLAGS += -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} -fPIC -c -O0 -g3
DEMO_PORT=7781

default: ${LIBNAME}.so

${DMTCP_ROOT}/test/dmtcp1: ${DMTCP_ROOT}/test
	cd ${DMTCP_ROOT}/test; make dmtcp1
check: ${LIBNAME}.so ${DMTCP_ROOT}/test/dmtcp1
	# Note that full path of plugin (using $$PWD in this case) is required.
	PROCESS_NAME="abc" ${DMTCP_ROOT}/bin/dmtcp_launch --port ${DEMO_PORT} -i 5 \
	  --with-plugin $$PWD/${LIBNAME}.so ${DMTCP_ROOT}/test/dmtcp1

# We link the library using C++ for compatibility with the main libdmtcp.so
${LIBNAME}.so: ${LIBOBJS}
	${CC} -shared -fPIC -o $@ $^ -lrt

.c.o:
	${CC} ${CFLAGS} -o $@ $< -lrt
.cpp.o:
	${CXX} ${CXXFLAGS} -o $@ $<

runtest: ${LIBNAME}.so test.c tidy
	${CC} -I${DMTCP_INCLUDE} -fPIC -g3 -O0 test.c -o test
	${CC} -fPIC -g3 -O0 ${DMTCP_ROOT}/test/dmtcp1.c -o dmtcp1
	STATFILE=./test-stats.log DMTCP_KILL_ON_RESUME_STRATEGY=1 ${DMTCP_ROOT}/bin/dmtcp_launch --with-plugin $(PWD)/${LIBNAME}.so ./test
	STATFILE=./test-stats.log DMTCP_KILL_ON_RESUME_STRATEGY=1 ${DMTCP_ROOT}/bin/dmtcp_restart ckpt*.dmtcp

runtest2: ${LIBNAME}.so tidy
	${CC} -g3 -O0 ${DMTCP_ROOT}/test/dmtcp1.c -o dmtcp1
	STATFILE=./dmtcp1-stats.log DMTCP_KILL_ON_RESUME_STRATEGY=1 ${DMTCP_ROOT}/bin/dmtcp_launch -i 5 --with-plugin $(PWD)/${LIBNAME}.so ./dmtcp1
	STATFILE=./dmtcp1-stats.log DMTCP_KILL_ON_RESUME_STRATEGY=1 ${DMTCP_ROOT}/bin/dmtcp_restart -i 10 ckpt*.dmtcp &
	@sleep 5
	@pkill -37 dmtcp1

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp test-stats.log*

clean:
	rm -f ${LIBOBJS} ${LIBNAME}.so

distclean: clean
	rm -f ${LIBNAME}.so *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp

dist: distclean
	dir=`basename $$PWD`; cd ..; \
	  tar czvf $$dir.tar.gz --exclude-vcs ./$$dir
	dir=`basename $$PWD`; ls -l ../$$dir.tar.gz

.PHONY: default clean dist distclean
