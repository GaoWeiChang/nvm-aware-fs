#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int fd, i;

    // simulate nvm write 200 times
    for(i = 0; i < 1000; i++){
        fd = open("testfile", O_CREATE | O_WRONLY);
        if(fd < 0){
            printf("nvm_test: open failed\n");
            exit(1);
        }
        write(fd, "hello nvm", 9);
        close(fd);
    }

    printf("nvm_test: done writing, checking stats...\n");
    nvmstats();

    exit(0);

    return 0;
}