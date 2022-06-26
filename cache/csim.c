#include "cachelab.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

int   verbose  = 0;
int   set_bits = 0;
int   set_num  = 0;
int   assoc    = 0;
int   blk_bits = 0;
char *trace_filename;

int hit_cnt   = 0;
int miss_cnt  = 0;
int evict_cnt = 0;

typedef struct {
    int valid;
    int tag;
    int stamp;
} block_t, *set_t, **cache_t;

cache_t cache = NULL;

void printHelpInfo()
{
    printf("Usage: ./csim-ref [-hv] -s <num> -E <num> -b <num> -t <file>\n"
           "Options:\n"
           "  -h         Print this help message.\n"
           "  -v         Optional verbose flag.\n"
           "  -s <num>   Number of set index bits.\n"
           "  -E <num>   Number of lines per set.\n"
           "  -b <num>   Number of block offset bits.\n"
           "  -t <file>  Trace file.\n"
           "\n"
           "Examples:\n"
           "  linux>  ./csim-ref -s 4 -E 1 -b 4 -t traces/yi.trace\n"
           "  linux>  ./csim-ref -v -s 8 -E 2 -b 4 -t traces/yi.trace\n");
}

// Initialize the cache
void cacheInit()
{
    cache = (cache_t)malloc(sizeof(set_t) * set_num);
    for (int i = 0; i < set_num; i++) {
        cache[i] = (set_t)malloc(sizeof(block_t) * assoc);
        for (int j = 0; j < assoc; j++) {
            cache[i][j].valid = 0;
            cache[i][j].tag   = 0;
            cache[i][j].stamp = 0;
        }
    }
}

// Destory the cache
void cacheDestory()
{
    for (int i = 0; i < set_num; i++)
        free(cache[i]);

    free(cache);
}

// Update the stamp in cache
void cacheUpdateStamp(int set, unsigned int tag)
{
    for (int i = 0; i < assoc; i++) {
        if (cache[set][i].valid == 0)
            continue;
        if (cache[set][i].tag == tag) {
            cache[set][i].stamp = 0;
        } else {
            cache[set][i].stamp++;
        }
    }
}

// Replace the cache line with the highest stamp or valid bit 0
void cacheReplace(int set, unsigned int tag)
{
    int max_idx   = -1;
    int max_stamp = -1;

    for (int i = 0; i < assoc; i++) {
        // invalid line
        if (cache[set][i].valid == 0) {
            cache[set][i].valid = 1;
            cache[set][i].tag   = tag;
            cacheUpdateStamp(set, tag);
            return;
        }

        if (cache[set][i].stamp > max_stamp) {
            max_stamp = cache[set][i].stamp;
            max_idx   = i;
        }
    }

    evict_cnt++;
    if (verbose)
        printf(" eviction");

    cache[set][max_idx].valid = 1;
    cache[set][max_idx].tag   = tag;
    cacheUpdateStamp(set, tag);
}

// Touch a line in the cache
void cacheTouch(unsigned int addr)
{
    int set = (addr >> blk_bits) & ((1 << set_bits) - 1);
    int tag = addr >> (blk_bits + set_bits);
    for (int i = 0; i < assoc; i++) {
        if (cache[set][i].valid && cache[set][i].tag == tag) {
            hit_cnt++;
            if (verbose)
                printf(" hit");
            cacheUpdateStamp(set, tag);
            return;
        }
    }

    miss_cnt++;
    if (verbose)
        printf(" miss");

    cacheReplace(set, tag);
    return;
}

// Run the trace file
void phaseTrace(FILE *fd)
{
    char         oper;
    unsigned int addr;
    int          size;
    while (fscanf(fd, " %c %x,%d", &oper, &addr, &size) > 0) {
        if (verbose)
            printf("%c %x,%d", oper, addr, size);

        switch (oper) {
        case 'I':
            break;

        case 'L':
        case 'S':
            cacheTouch(addr);
            break;

        case 'M':
            cacheTouch(addr);
            cacheTouch(addr);
            break;

        default:
            break;
        }
        if (verbose)
            putchar(10);
    }
}

int main(int argc, char *argv[])
{
    // get the options
    int opt;
    while ((opt = getopt(argc, argv, "vhs:E:b:t:")) != -1) {
        switch (opt) {
        case 'h':
            printHelpInfo();
            return 0;

        case 'v':
            verbose = 1;
            break;

        case 's':
            set_bits = atoi(optarg);
            break;

        case 'E':
            assoc = atoi(optarg);
            break;

        case 'b':
            blk_bits = atoi(optarg);
            break;

        case 't':
            trace_filename = optarg;
            break;

        default:
            fprintf(stderr, "Invalid Option: %c", (char)opt);
            printHelpInfo();
            return -1;
        }
    }

    if (set_bits <= 0 || assoc <= 0 || blk_bits <= 0 || trace_filename == NULL) {
        printHelpInfo();
        return -1;
    }
    set_num = 1 << set_bits;

    // open the trace file
    FILE *trace_fp = fopen(trace_filename, "r");
    if (trace_fp == NULL) {
        printf("Error: could not open trace file %s\n", trace_filename);
        return -1;
    }

    // initialize the cache
    cacheInit();

    // run the trace file
    phaseTrace(trace_fp);

    // deallocate memory
    cacheDestory();
    fclose(trace_fp);

    printSummary(hit_cnt, miss_cnt, evict_cnt);
    return 0;
}
