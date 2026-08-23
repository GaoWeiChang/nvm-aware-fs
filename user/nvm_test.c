#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int fd, i, n;
    char buf[16];
    char *msg = "hello nvm";
    int msglen = 9;

    // simulate nvm write 200 times
    for(i = 0; i < 200; i++){
        fd = open("testfile", O_CREATE | O_WRONLY);
        if(fd < 0){
            printf("nvm_test: open failed\n");
            exit(1);
        }
        if(write(fd, msg, msglen) != msglen){
            printf("nvm_test: write failed on iteration %d\n", i);
            close(fd);
            exit(1);
        }
        close(fd);

        // read back immediately and verify correctness
        fd = open("testfile", O_RDONLY);
        if(fd < 0){
            printf("nvm_test: reopen for read failed on iteration %d\n", i);
            exit(1);
        }
        memset(buf, 0, sizeof(buf));
        n = read(fd, buf, msglen);
        close(fd);

        if(n != msglen || memcmp(buf, msg, msglen) != 0){
            printf("nvm_test: verify failed on iteration %d (n=%d, buf=%s)\n", i, n, buf);
            exit(1);
        }
    }

    printf("nvm_test: done writing and verifying %d times, checking stats...\n", 200);
    nvmstats();

    exit(0);

    return 0;
}