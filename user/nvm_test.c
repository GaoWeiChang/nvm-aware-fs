#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "user/user.h"

// fill a block-sized buffer with a pattern that depends on the block index,
// so we can tell blocks apart when reading back.
static void
fill_block(char *buf, int blockno)
{
    int i;
    for (i = 0; i < BSIZE; i++)
        buf[i] = (char)((blockno * 7 + i) % 251);
}

static void
direct_block_test(void)
{
    int fd, i, n;
    char buf[16];
    char *msg = "hello nvm";
    int msglen = 9;

    // simulate nvm write 200 times, always within the direct blocks
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

    printf("nvm_test: direct-block test passed (%d iterations)\n", 200);
}

// Write enough sequential blocks to a single file that it grows past
// NDIRECT blocks and starts using the (not-yet-remapped) indirect block,
// then read everything back and verify block-by-block.
static void
indirect_block_test(void)
{
    int fd, i;
    char wbuf[BSIZE], rbuf[BSIZE];
    // go a good bit past the NDIRECT boundary so we exercise several
    // indirect-mapped blocks, not just the first one.
    int nblocks = NDIRECT + 8;

    printf("nvm_test: indirect-block test, NDIRECT=%d, writing %d blocks (%d bytes)\n",
           NDIRECT, nblocks, nblocks * BSIZE);

    fd = open("bigfile", O_CREATE | O_TRUNC | O_WRONLY);
    if(fd < 0){
        printf("nvm_test: open bigfile for write failed\n");
        exit(1);
    }

    for(i = 0; i < nblocks; i++){
        fill_block(wbuf, i);
        if(write(fd, wbuf, BSIZE) != BSIZE){
            printf("nvm_test: write failed at block %d (direct-only up to block %d)\n",
                   i, NDIRECT - 1);
            close(fd);
            exit(1);
        }
    }
    close(fd);

    fd = open("bigfile", O_RDONLY);
    if(fd < 0){
        printf("nvm_test: reopen bigfile for read failed\n");
        exit(1);
    }

    for(i = 0; i < nblocks; i++){
        memset(rbuf, 0, BSIZE);
        if(read(fd, rbuf, BSIZE) != BSIZE){
            printf("nvm_test: read failed at block %d\n", i);
            close(fd);
            exit(1);
        }
        fill_block(wbuf, i);
        if(memcmp(rbuf, wbuf, BSIZE) != 0){
            printf("nvm_test: verify failed at block %d (%s indirect region)\n",
                   i, i < NDIRECT ? "direct" : "INDIRECT");
            close(fd);
            exit(1);
        }
    }
    close(fd);

    printf("nvm_test: indirect-block test passed (%d blocks, %d of them via indirect)\n",
           nblocks, nblocks - NDIRECT);
}

// Stress test for indirect-block
static void
indirect_wear_stress_test(void)
{
    int fd, round, i;
    char wbuf[BSIZE];
    int rounds = 200;          // number of blocks appended past NDIRECT
    int global_idx = 0;

    printf("nvm_test: indirect-wear stress test, appending %d blocks one at a time\n",
           rounds);

    fd = open("wearfile", O_CREATE | O_TRUNC | O_WRONLY);
    if(fd < 0){
        printf("nvm_test: open wearfile failed\n");
        exit(1);
    }

    // first, fill up to NDIRECT so every subsequent write forces bmap()
    // into the indirect path.
    for(i = 0; i < NDIRECT; i++){
        fill_block(wbuf, global_idx);
        if(write(fd, wbuf, BSIZE) != BSIZE){
            printf("nvm_test: stress warmup write failed at block %d\n", i);
            close(fd);
            exit(1);
        }
        global_idx++;
    }

    // now every write below re-reads/re-writes the SAME indirect block
    // to add one more pointer -- this is the repeated write we want to see.
    for(round = 0; round < rounds; round++){
        fill_block(wbuf, global_idx);
        if(write(fd, wbuf, BSIZE) != BSIZE){
            printf("nvm_test: stress write failed at round %d\n", round);
            close(fd);
            exit(1);
        }
        global_idx++;
    }

    close(fd);

    // sanity check: read everything back
    fd = open("wearfile", O_RDONLY);
    if(fd < 0){
        printf("nvm_test: reopen wearfile for read failed\n");
        exit(1);
    }
    char rbuf[BSIZE];
    for(i = 0; i < global_idx; i++){
        memset(rbuf, 0, BSIZE);
        if(read(fd, rbuf, BSIZE) != BSIZE){
            printf("nvm_test: stress read failed at block %d\n", i);
            close(fd);
            exit(1);
        }
        fill_block(wbuf, i);
        if(memcmp(rbuf, wbuf, BSIZE) != 0){
            printf("nvm_test: stress verify failed at block %d\n", i);
            close(fd);
            exit(1);
        }
    }
    close(fd);

    printf("nvm_test: indirect-wear stress test passed (%d total blocks, %d via indirect)\n",
           global_idx, rounds);
}

int
main(int argc, char *argv[])
{
    direct_block_test();
    indirect_block_test();
    indirect_wear_stress_test();

    printf("nvm_test: done, checking stats...\n");
    nvmstats();

    exit(0);

    return 0;
}