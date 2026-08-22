// NVM simulator

#define NVM_BLOCKS          1000    // number of block
#define NVM_ENDURANCE       100000  // block endurance
#define NVM_READ_PENALTY    1
#define NVM_WRITE_PENALTY   10      // additional penalty for write
#define NVM_WEAR_SKEW_LIMIT 5       // proactively remap once the block has worn far ahead of the least-worn block
struct nvm_block_info {
    uint32 write_count;
    uint32 read_count;
    uint8 is_worn_out;
};

void nvm_init(void);
void nvm_write(uint blockno);
void nvm_read(uint blockno);
void nvm_print_block_used(void);
void nvm_print_stats(const char *name, uint start, uint end);
int nvm_is_worn_out(uint blockno);
uint nvm_least_worn_block(void);        // find the least worn block
uint nvm_get_write_count(uint blockno);

