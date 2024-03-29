//Source file for virtual memory implementation

 /*
  *Virtual Memory Implementation
  *Single level 32-Bit page table
  *****fifo, optimal, aging
  */

#include "vmsim.h"
#include "hashmap.h"

void* allocate(unsigned long int size) {
    void* ptr = NULL;
    if(size) {
        ptr = malloc(size);
        if(!ptr) {
            fprintf(stderr, "Error on mallocing memory\n");
            exit(1);
        }
        memset(ptr, 0, size);
    }
    return(ptr);
}

// void* deallocate(void** ptr, unsigned long int size){
//     if(size){
//       ptr = free(size);
//     }
//     return(ptr);
// }

void toupper_str(char* str){
  char* c = str;
  while(*c){
    *c = toupper(*c);
    c++;
  }
}

int main(int argc, char* argv[]) {
    //Check for input arguments
    //Store into array as variables so we can access the input data
    //Open desired trace file

    if((argc != 6) && (argc != 8)) {
        fprintf(stderr, "USAGE: %s -n <numframes> -a <fifo|opt|aging> [-r <refresh>] <tracefile>\n", argv[0]);
        exit(1);
    }

    numframes = atoi(argv[2]);
    char* algorithm = argv[4];
    toupper_str(algorithm);

    char* filename;
    unsigned int refresh;

    if(argc == 6){
      filename = argv[5];
    } else {
      filename = argv[7];
      refresh = atoi(argv[6]);
    }

    FILE* file = fopen(filename,"rb");
    if(!file) {
        fprintf(stderr, "Error on opening %s\n", filename);
        exit(1);
    }

    /*
     * Calculate the trace file's length
     * and read in the trace file
     * and write it into addr_arr and mode_arr arrays
     */

    unsigned int numaccesses = 0;
    unsigned char mode = '\0';
    unsigned int addr = 0;
    unsigned int cycles = 0;

    // Calculate number of lines in the trace file
    while(fscanf(file, "%c %x %d\n", &mode, &addr, &cycles) == 3) {
        numaccesses++;
    }
    rewind(file);
    //Parse each line
    unsigned char mode_array[numaccesses];
    unsigned int address_array[numaccesses];
    unsigned int cycles_array[numaccesses];

    unsigned int i = 0;
    // Store the memory accesses
    while(fscanf(file, "%c %x %d\n", &mode_array[i], &address_array[i], &cycles_array[i]) == 3) {
        i++;
    }

    if(fclose(file)) {
        fprintf(stderr, "Error on closing %s\n", filename);
        exit(1);
    }

    if(i != numaccesses) {
        fprintf(stderr, "Arrays are populated incorrectly\n");
        exit(1);
    }

    // Initialize the physical memory address space
    long frame_size = PAGE_SIZE_4KB;// / PAGE_SIZE_UNITS;
    long memory_size = frame_size * numframes;
    physical_frames = (unsigned int*) allocate(memory_size);

    // Create the first frame of the frames linked list
    struct frame_struct* head = NULL;
    struct frame_struct* curr = NULL;
    for(i = 0; i < numframes; i++) {
        if(i == 0) {
            head = (struct frame_struct*) allocate(sizeof(struct frame_struct));
            curr = head;
        }
        else {
            curr->next = (struct frame_struct*) allocate(sizeof(struct frame_struct));
            curr = curr->next;
        }
        curr->frame_number = i;
        curr->physical_address = physical_frames + (i * frame_size);
        curr->virtual_address = -1;
        curr->pte_pointer = NULL;
    }

    // Initialize page table
    long page_table_size = PT_SIZE_1MB * PTE_SIZE_BYTES;
    page_table = (struct pte_32**) allocate(page_table_size);

    // Initialize hashmap for opt
    vm = (struct hashmapbase*) hashmap_initialize((unsigned int) PT_SIZE_1MB);
    // Fill in the hashmap with the key being the page number and
    // the value being the set of line number at which this page is being accessed
    for(i = 0; i < numaccesses; i++) {
        unsigned int key = PTE32_INDEX(address_array[i]);
        unsigned int value = i;
        hashmap_insert(vm, key, value);
    }

    //hashmap_print(vm);

    struct pte_32* new_pte = NULL;
    unsigned char mode_type = '\0';
    unsigned int fault_address = 0;

    unsigned int num_cycles = 0;
    int hit = 0;
    int page2evict = 0;
    int numfaults = 0;
    int numwrites = 0;

    // Main loop to process memory accesses
    for(i = 0; i < numaccesses; i++) {
        //printf("Line #: %d\n", i);
        //num_cycles = num_cycles + cycles_array[i];
        fault_address = address_array[i];
        mode_type = mode_array[i];
        hit = 0;

        // Perform page walk for the fault address
        new_pte = (struct pte_32*) handle_page_fault(fault_address);

        if(mode_type == 's'){
          new_pte->dirty = 1;
        }

        // Keep a reference counter (8bit counter that we have)(shift right at the beginning of loop, replacing the reference bit as the leftmost) and the reference bit in each PTE
          // do a check to clear the reference bit, set reference bit to 0
          // Every k cycles(checking if cycles >= refresh), shift counter right one bit -> insert reference bit as the leftmost bit of the counter -> clear reference bit
        // When page is brought into memory (aka when we swap in a page) (set counter = 10000000 and reference bit = 0 (cleared))
        if(!strcmp(algorithm, "AGING")){
            if(i == 0){
              num_cycles = cycles_array[i];
            } else {
              num_cycles += cycles_array[i] + 1;
            }

            if(num_cycles % refresh == 0){
              //traverse the frames linked list to shift each frame's pte's counter right one bit
              while(num_cycles <= refresh){
                curr = head;
                while(curr){
                  if(curr->pte_pointer == NULL){
                    break;
                  }
                  curr->pte_pointer->reference_counter = curr->pte_pointer->reference_counter >> 1; //shifts right and a 0 into the msb
                  if(curr->pte_pointer->reference_bit == 1){
                    curr->pte_pointer->reference_counter = curr->pte_pointer->reference_counter | (1<<7); //shifts a 1 into the msb
                  }
                  curr->pte_pointer->reference_bit = 0; //clear reference bit
                  curr = curr->next;
                }
                num_cycles = num_cycles - refresh;
              }
              num_cycles = 1;
            }
        }


        /*
         * Traverse the frames linked list
         * to see if the requested page is present in
         * the frames linked list.
         */
        curr = head;
        while(curr) {
            if(curr->physical_address == new_pte->physical_address) {
                if(new_pte->present) {
                    curr->virtual_address = fault_address;
                    hashmap_peek(vm, PTE32_INDEX(curr->virtual_address)); //make sure to delete the indexes of the hashmap if it's been accessed
                    curr->pte_pointer->reference_bit = 1; // if it is a hit, reference bit must be 1
                    hit = 1;
                    #ifdef DEBUG
                    printf("%5d: page hit   – no eviction %010u (0x%08x)\n", i, (unsigned int) ((uintptr_t) fault_address), fault_address);
                    printf("%5d: page hit   - keep page   %010u (0x%08x) accessed (0x%08x)\n", i, (unsigned int) ((uintptr_t) curr->physical_address), *((unsigned int*) &new_pte->physical_address), curr->virtual_address);
                    #endif
                }
                break;
            }
            else {
                curr = curr->next;
            }
        }

        /*
         * If the requested page is not present in the
         * frames linked list use the requested page replacement
         * to evict the victim frame and swap in the requested frame
         */
        if(!hit) {
            if(!strcmp(algorithm, "FIFO")) { // Fifo page replacement algorithm
                page2evict = fifo();
            } else if(!strcmp(algorithm, "OPT")){ // Opt page replacement algorithm

                /**
                //current page is not in the frame linked list, thus we must now traverse
                //through the linked list again and find out which of those values are
                //the furthest accessed. the index at top that is greater will be evicted
                **/

                // Traverse through the frames linked list to determine which
                // is accessed furthest in the future by checking with the hashmap(smallest number)
                curr = head;
                unsigned int key; // Get the key of the current frame
                struct value_object* top; // Get the value associated with the key
                unsigned int maxIndex = 0; // Get the index and set that as the max
                struct frame_struct* evictThis;
                while(curr) {
                    key = PTE32_INDEX(curr->virtual_address);
                    top = hashmap_top(vm, key);
                    if(top == NULL){ //the key is null, evict
                      evictThis = curr;
                      break;
                    } else {
                        if(top->value > maxIndex){
                          maxIndex = top->value;
                          evictThis = curr;
                        }
                    }
                    curr = curr->next;
                }
                page2evict = evictThis->frame_number;
            } else if (!strcmp(algorithm, "AGING")){

                // To select which page to evict...
                // Pick the page with the smallest(lowest) counter value to evict
                // checking each one's age and selecting the smallest age
                // if there is a tie... evict a non dirty page over a dirty page. if both are dirty or both non dirty, evict the smaller virtual address (ie if 0x200, vs 0x00, evict 0x000)

                struct frame_struct* evictThis;
                uint8_t lowest_counter = 9;
                curr = head;
                while(curr){
                  if(curr->pte_pointer == NULL){
                    evictThis = curr;
                    break;
                  }

                  if(curr->pte_pointer->reference_counter < lowest_counter){ //look for the lowest counter to evict
                    lowest_counter = curr->pte_pointer->reference_counter;
                    evictThis = curr;
                  } else if(curr->pte_pointer->reference_counter == lowest_counter){ //there is a tie, we must break it
                      if(curr->pte_pointer->dirty == 0 && evictThis->pte_pointer->dirty == 1){ //evict the non dirty page
                        lowest_counter = curr->pte_pointer->reference_counter;
                        evictThis = curr;
                      } else if((curr->pte_pointer->dirty == 0 && evictThis->pte_pointer->dirty == 0) || (curr->pte_pointer->dirty == 1 && evictThis->pte_pointer->dirty == 1)){ //if both are either non dirty or dirty, evict the smaller virtual address
                          if(curr->virtual_address < evictThis->virtual_address){
                            lowest_counter = curr->pte_pointer->reference_counter;
                            evictThis = curr;
                          }
                      }
                  }

                  curr = curr->next;
                }
                if(evictThis->pte_pointer != NULL){
                  //Set the page to be evicted's counter and reference bit to 0
                  evictThis->pte_pointer->reference_counter = 0;
                  evictThis->pte_pointer->reference_bit = 0;
                }
                page2evict = evictThis->frame_number;
            }

           /* Traverse the frames linked list to
            * find the victim frame and swap it out
            * Set the present bit and collect some statistics
            */
            curr = head;
            while(curr) {
                if(curr->frame_number == page2evict) {
                    numfaults++;

                    if(curr->pte_pointer) {
                        curr->pte_pointer->present = 0;
                        //dirty bit implementation
                        if(curr->pte_pointer->dirty) {
                          numwrites++;
                          curr->pte_pointer->dirty = 0;
                        }
                    }

                    curr->pte_pointer = (struct pte_32*) new_pte;
                    new_pte->physical_address = curr->physical_address;
                    new_pte->present = 1;
                    curr->virtual_address = fault_address;
                    // Set the new frame's PTE's counter to 0x80 and reference bit to 0
                    curr->pte_pointer->reference_counter = 0x80;
                    curr->pte_pointer->reference_bit = 0;
                    hashmap_peek(vm, PTE32_INDEX(curr->virtual_address)); //Delete the values of the hashmap if it's been accessed

		                break;
                }
                curr = curr->next;
            }
        }
    }

    // Loop over the page table and free the pages and then free the whole page table
    unsigned int j = 0;
    for(j = 0; j < PT_SIZE_1MB; j++){
      free(page_table[j]);
    }
    free(page_table);

    printf("Algorithm: %s\n", algorithm);
    printf("Number of frames: %d\n", numframes);
    printf("Total memory accesses: %d\n", i);
    printf("Total page faults: %d\n", numfaults);
    printf("Total writes to disk: %d\n", numwrites);

    return(0);
}

struct pte_32* handle_page_fault(unsigned int fault_address) {
    pte = (struct pte_32*) page_table[PTE32_INDEX(fault_address)];
    if(!pte) {
        pte = (struct pte_32*) allocate(sizeof(struct pte_32));
        pte->present = 0;
        pte->physical_address = NULL;
        page_table[PTE32_INDEX(fault_address)] = (struct pte_32*) pte;
    }
    #ifdef DEBUG
    printf("############## Page fault handler ##############\n");
    printf("Fault address:           %010u (0x%08x)\n", (unsigned int) ((uintptr_t) fault_address), fault_address);
    printf("Page table base address: %010u (0x%08x)\n", (unsigned int) ((uintptr_t) page_table), *((unsigned int*) &page_table));
    printf("PTE offset:              %010u (0x%08x)\n", PTE32_INDEX(fault_address), PTE32_INDEX(fault_address));
    printf("PTE index:               %010u (0x%08x)\n", (unsigned int) ((uintptr_t)(PTE32_INDEX(fault_address) * PTE_SIZE_BYTES)), (unsigned int) ((uintptr_t)(PTE32_INDEX(fault_address) * PTE_SIZE_BYTES)));
    printf("PTE virtual address:     %010u (0x%08x)\n", (unsigned int) ((uintptr_t) page_table + PTE32_INDEX(fault_address)), *((unsigned int*) &*page_table + PTE32_INDEX(fault_address)));
    printf("Physcial base address:   %010u (0x%08x)\n", (unsigned int) ((uintptr_t) pte->physical_address), *((unsigned int*) &pte->physical_address));
    printf("Frame offset:            %010u (0x%08x)\n", FRAME_INDEX(fault_address), FRAME_INDEX(fault_address));
    printf("Frame index:             %010u (0x%08x)\n", FRAME_INDEX(fault_address) * PAGE_SIZE_UNITS, FRAME_INDEX(fault_address) * PAGE_SIZE_UNITS);
    printf("Frame physical address:  %010u (0x%08x)\n", (unsigned int) ((uintptr_t) pte->physical_address + FRAME_INDEX(fault_address)), *((unsigned int*) &pte->physical_address + FRAME_INDEX(fault_address)));
    #endif

    return ((struct pte_32*) pte);
}

unsigned int fifo() {
    ++current_index;
    current_index = current_index % numframes;
    return (current_index);
}
