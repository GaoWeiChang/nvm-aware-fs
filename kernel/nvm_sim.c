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
nvm_print_stats(void)
{
    uint32 total_writes = 0;
    uint32 total_reads = 0;
    uint32 max_wear = 0;
    uint32 min_wear = 0xFFFFFFFF;
    int worn_out_count = 0;
    int used_blocks = 0;

    acquire(&nvm.lock);
    for(int i=0; i<NVM_BLOCKS; i++){
        uint32 w = nvm.nvm_table[i].write_count;
        total_writes += w;
        total_reads += nvm.nvm_table[i].read_count;

        if(w > 0){
            used_blocks++;
            if(w > max_wear)
                max_wear = w;
            if(w < min_wear)
                min_wear = w;
        }
        if(nvm.nvm_table[i].is_worn_out)
            worn_out_count++;
    }
    release(&nvm.lock);

    printk("=== NVM Stats ===\n");
    printk("total writes: %d, total reads: %d\n", total_writes, total_reads);
    printk("used blocks: %d / %d\n", used_blocks, NVM_BLOCKS);
    printk("worn-out blocks: %d\n", worn_out_count);
    if(used_blocks > 0)
        printk("wear range: min=%d, max=%d, spread=%d\n", 
                min_wear, max_wear, max_wear - min_wear);
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