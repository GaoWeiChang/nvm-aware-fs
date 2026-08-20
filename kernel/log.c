#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
  int n;                  // amount of blocks in this transaction
  uint seq;               // monotonically increasing, boot picks the highest
  int block[LOGBLOCKS];   // real disk block number for each logged block
  int slot[LOGBLOCKS];    // physical slot within the log data region
};

struct log {
  struct spinlock lock;
  int hstart;      // base block of the K rotating header slots
  int dstart;      // base block of the log data area
  int hcur;        // index of the slot holding the current header
  uint seq;        // sequence number of the header last written
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;
  int ncommit;
  struct logheader lh;
};
struct log log;

static void recover_from_log(void);
static void commit();

void
initlog(int dev, struct superblock *sb)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  initlock(&log.lock, "log");
  log.hstart = sb->logstart;
  log.dstart = sb->logstart + LOG_HDR_SLOTS;
  log.dev = dev;
  recover_from_log();
}

// Copy committed blocks from log to their home location
static void
install_trans(int recovering)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    if (recovering) {
      printk("recovering tail %d dst %d\n", tail, log.lh.block[tail]);
    }
    struct buf *lbuf = bread(log.dev, log.dstart + log.lh.slot[tail]); // read log block
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]);   // read dst
    memmove(dbuf->data, lbuf->data, BSIZE); // copy block to dst
    bwrite(dbuf);                           // write dst to disk
    if (recovering == 0)
      bunpin(dbuf);
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void
read_head(void)
{
  /*
    Scan the K header slots and load the one hold the 
    highest sequence number (most recently written) into the in-memory log header
  */

  struct buf *bufs[LOG_HDR_SLOTS];
  int best_slot = 0;
  uint best_seq = 0;

  for (int slot = 0; slot < LOG_HDR_SLOTS; slot++) {
    bufs[slot] = bread(log.dev, log.hstart + slot);
    struct logheader *hb = (struct logheader *)(bufs[slot]->data);
    if (slot == 0 || hb->seq >= best_seq) {
      best_seq = hb->seq;
      best_slot = slot;
    }
  }

  struct logheader *hb = (struct logheader *)(bufs[best_slot]->data);
  log.hcur = best_slot;
  log.seq = best_seq;
  log.lh.n = hb->n;
  log.lh.seq = hb->seq;
  for (int i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = hb->block[i];
    log.lh.slot[i] = hb->slot[i];
  }

  for (int slot = 0; slot < LOG_HDR_SLOTS; slot++)
    brelse(bufs[slot]);
}

// Write in-memory log header to disk, into the next slot in the rotation
// This is the true point at which the
// current transaction commits.
static void
write_head(void)
{
  log.hcur = (log.hcur + 1) % LOG_HDR_SLOTS;
  log.seq++;

  struct buf *buf = bread(log.dev, log.hstart + log.hcur);
  struct logheader *hb = (struct logheader *)(buf->data);
  int i;
  hb->n = log.lh.n;
  hb->seq = log.seq;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
    hb->slot[i] = log.lh.slot[i];
  }
  bwrite(buf);
  brelse(buf);

  log.lh.seq = log.seq;
}

static void
recover_from_log(void)
{
  read_head();
  install_trans(1); // if committed, copy from log to disk
  log.lh.n = 0;
  write_head(); // clear the log
}

// called at the start of each FS system call.
void
begin_op(void)
{
  acquire(&log.lock);
  while (1) {
    if (log.committing) {
      sleep_prepare(&log);
      release(&log.lock);
      sleep();
      acquire(&log.lock);
    } else if (log.lh.n + (log.outstanding + 1) * MAXOPBLOCKS > LOGBLOCKS) {
      // this op might exhaust log space; wait for commit.
      sleep_prepare(&log);
      release(&log.lock);
      sleep();
      acquire(&log.lock);
    } else {
      log.outstanding += 1;
      release(&log.lock);
      break;
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if (log.committing)
    panic("log.committing");
  if (log.outstanding == 0) {
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
  }
  release(&log.lock);

  if (do_commit) {
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;
    log.ncommit += 1;
    wakeup(&log);
    release(&log.lock);
  }
}

// Copy modified blocks from cache to log.
static void
write_log(void)
{
  /*
    slots are picked by lowest wear count so writes spread evenly 
    and low-numbered slots don't wear out faster than the rest
  */
  int used[LOGBLOCKS] = {0};
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    // find best wear
    int best = -1;
    uint best_wear = 0xFFFFFFFF;

    for (int s = 0; s < LOGBLOCKS; s++) {
      if (used[s])
        continue;
      uint w = nvm_get_write_count(log.dstart + s);
      if (w < best_wear) {
        best_wear = w;
        best = s;
      }
    }

    used[best] = 1;
    log.lh.slot[tail] = best;

    struct buf *to = bread(log.dev, log.dstart + best); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);
    bwrite(to); // write the log
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  if (log.lh.n > 0) {
    write_log();      // Write modified blocks from cache to log
    write_head();     // Write header to disk -- the real commit
    install_trans(0); // Now install writes to home locations
    log.lh.n = 0;
    write_head(); // Erase the transaction from the log
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  int i;

  acquire(&log.lock);
  if (log.lh.n >= LOGBLOCKS)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno) // log absorption
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n) { // Add new block to log?
    bpin(b);
    log.lh.n++;
  }
  release(&log.lock);
}

uint64
sys_sync(void)
{
  acquire(&log.lock);
  if (log.committing || log.outstanding > 0) {
    int n = log.ncommit + 1;
    while (log.ncommit < n) {
      sleep_prepare(&log);
      release(&log.lock);
      sleep();
      acquire(&log.lock);
    }
  }
  release(&log.lock);
  return 0;
}
