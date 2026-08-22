#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "nvm_sim.h"

static struct {
    struct spinlock lock;
    struct nvm_block_info nvm_table[NVM_BLOCKS];
} nvm;

// simulate latency for nvm operation
static void
nvm_delay(uint units)
{
    for(volatile int i=0; i < units * 1000; i++);
}


void 
nvm_init(void)
{
    initlock(&nvm.lock, "nvm");

    acquire(&nvm.lock);
    memset(nvm.nvm_table, 0, sizeof(nvm.nvm_table));
    release(&nvm.lock);
}

void 
nvm_write(uint blockno)
{
    if(blockno >= NVM_BLOCKS)
        return;
    
    nvm_delay(NVM_WRITE_PENALTY);

    acquire(&nvm.lock);
    nvm.nvm_table[blockno].write_count++;

    if(nvm.nvm_table[blockno].write_count >= NVM_ENDURANCE){
        nvm.nvm_table[blockno].is_worn_out = 1;
        printk("nvm_sim: block %d worn out!\n", blockno);
    }
    release(&nvm.lock);
}

void 
nvm_read(uint blockno)
{
    if(blockno >= NVM_BLOCKS)
        return;

    nvm_delay(NVM_READ_PENALTY);

    acquire(&nvm.lock);
    nvm.nvm_table[blockno].read_count++;
    release(&nvm.lock);
}

void
nvm_print_block_used(void)
{
    int worn_out_count = 0;
    int used_blocks = 0;

    acquire(&nvm.lock);
    for(int i=0; i<NVM_BLOCKS; i++){
        uint32 w = nvm.nvm_table[i].write_count;

        if(w > 0)
            used_blocks++;
        if(nvm.nvm_table[i].is_worn_out)
            worn_out_count++;
    }
    release(&nvm.lock);

    printk("used blocks: %d / %d\n", used_blocks, NVM_BLOCKS);
    printk("worn-out blocks: %d\n", worn_out_count);
}

void
nvm_print_stats(const char *name, uint start, uint end)
{
    uint32 total_writes = 0;
    uint32 max_wear = 0;
    uint32 min_wear = 0xFFFFFFFF;
    int max_wear_block = -1;
    int min_wear_block = -1;
    int used_blocks = 0;

    if(end > NVM_BLOCKS)
        end = NVM_BLOCKS;

    acquire(&nvm.lock);
    for(uint i = start; i < end; i++){
        uint32 w = nvm.nvm_table[i].write_count;
        total_writes += w;
        if(w > 0){
            used_blocks++;
            if(w > max_wear){ 
                max_wear = w; 
                max_wear_block = i; 
            }
            if(w < min_wear){ 
                min_wear = w; 
                min_wear_block = i; 
            }
        }
    }
    release(&nvm.lock);

    if(used_blocks > 0)
        printk("[%s] blocks %d-%d: writes=%d, wear min=%d (block %d), max=%d (block %d), spread=%d\n",
            name, start, end - 1, total_writes, min_wear, min_wear_block, max_wear, max_wear_block, max_wear - min_wear);
    else
        printk("[%s] blocks %d-%d: writes=%d, (no writes)\n", name, start, end - 1, total_writes);
}

int 
nvm_is_worn_out(uint blockno)
{
    if(blockno >= NVM_BLOCKS)
        return 1;
    
    return nvm.nvm_table[blockno].is_worn_out;
}

uint 
nvm_least_worn_block(void)
{
    uint min_count = 0xFFFFFFFF;
    uint best = 0;

    acquire(&nvm.lock);
    // wear leveling: find the non-worn block with the lowest write count
    for(int i=0; i<NVM_BLOCKS; i++){
        if(!(nvm.nvm_table[i].is_worn_out) && (nvm.nvm_table[i].write_count < min_count)){
            min_count = nvm.nvm_table[i].write_count;
            best = i;
        }
    }
    release(&nvm.lock);

    return best;
}

uint 
nvm_get_write_count(uint blockno)
{
    if(blockno >= NVM_BLOCKS)
        return 0xFFFFFFFF;
    
    acquire(&nvm.lock);
    uint count = nvm.nvm_table[blockno].write_count;
    release(&nvm.lock);

    return count;
}