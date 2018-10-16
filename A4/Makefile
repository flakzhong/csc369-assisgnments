all : ext2_mkdir ext2_cp ext2_ln ext2_rm ext2_restore ext2_checker

ext2_mkdir : ext2_mkdir.o
	gcc -Wall -g -o ext2_mkdir ext2_mkdir.o -lm
	
ext2_cp : ext2_cp.o
	gcc -Wall -g -o ext2_cp ext2_cp.o -lm
	
ext2_ln : ext2_ln.o
	gcc -Wall -g -o ext2_ln ext2_ln.o -lm
	
ext2_rm : ext2_rm.o
	gcc -Wall -g -o ext2_rm ext2_rm.o -lm
	
ext2_restore : ext2_restore.o
	gcc -Wall -g -lm -o ext2_restore ext2_restore.o
	
ext2_checker : ext2_checker.o
	gcc -Wall -g -o ext2_checker ext2_checker.o
	
%.o: %.c
	gcc -c -Wall -g -o $@ $<
	
clean:
	rm -f *.o ext2_mkdir ext2_cp ext2_ln ext2_mkdir ext2_cp ext2_ln ext2_rm ext2_restore ext2_checker