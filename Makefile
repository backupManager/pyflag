## Top level makefile
BIN	= bin/
DIRS	= iosubsys imagingtools/dd_rescue/ raidtools indextools regtools virustools sources
SYSBINS = cjpeg djpeg
PYTHONLIB = /usr/lib/python2.3/
PYTHONBIN = `which python2.3`
DATA_DIR = `grep -i DATA_DIR pyflag/pyflagrc | cut -d= -f2`
MYSQLCOMMAND =./bin_dist/mysql/bin/mysql --socket=bin_dist/mysql/data/pyflag.sock

all:	bins
	for dir in $(DIRS); do\
          (echo Entering directory `pwd`/$$dir; cd $$dir; make "CC=$(CC)" MAKELEVEL= ; echo leaving directory `pwd`/$$dir ); done

## Copy binaries from the system to put into the flag bin dir
bins:
	for i in $(SYSBINS); do cp `which $$i` $(BIN); done

clean:
	for dir in $(DIRS); do\
          (cd $$dir; make clean "CC=$(CC)" MAKELEVEL=); done

bin-dist:
	rm -rf bin_dist
	mkdir -p bin_dist
	cp -ar $(PYTHONLIB) bin_dist/
	for i in `find . -maxdepth 1 | egrep -v '(darcs|sources|bin_dist|^.$$)'`; do cp -a $$i bin_dist/; done
	cp $(PYTHONBIN) bin_dist/bin/python
	## Reset all the time stamps to 0
	find bin_dist/ -exec touch -a -d19700000 \{\} \;
	## Run the unit test to touch all the files
	cd bin_dist/ && PYTHONHOME=`pwd`/python2.3/ PYTHONPATH=`pwd`:`pwd`/python2.3/:`pwd`/python2.3/site-packages/:`pwd`/python2.3/lib-dynload ./bin/python pyflag/unit_test.py
	## Now we cleanup python core (any files that were not touched)
	find bin_dist/python2.3/ -atime +1 -exec rm {} \;

	## Delete source directories
	cd bin_dist/ && rm -rf sources sgzip regtools raidtools patches libevf iosubsys indextools exgrep docs virustools imagingtools

	## General cleanups
	find bin_dist/ -depth -name CVS -exec rm -rf {} \;
	find bin_dist/ -depth -empty -exec rmdir \{\} \; 
	find bin_dist/ -depth -name \*~ -exec rm -f \{\} \; 

	## Strip all binaries:
	find bin_dist/ -perm +0111 -exec strip {} \;

mysql:
	cd bin_dist/ && tar -xvzf ../sources/mysql*.tgz
	cd bin_dist/mysql/ && ./scripts/mysql_install_db
#	ln -s bin_dist/mysql/bin/mysql bin_dist/bin/
	## Now we launch the database in the background
	bash -c ' cd bin_dist/mysql &&  ./bin/mysqld_safe --skip-networking --socket=pyflag.sock --skip-grant-tables  --datadir=./data/ &'
	## Wait a bit for the server to start up
	sleep 1
	## Now we create the database:
	echo 'CREATE DATABASE pyflag;' | $(MYSQLCOMMAND)
	cat db.setup | $(MYSQLCOMMAND) pyflag