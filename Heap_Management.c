#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#define MEMORY_SIZE 1048576 // 1 megabite
#define NAME_SIZE 20
typedef enum {FAILURE,SUCCESS} status;
typedef enum {FALSE,TRUE} boolean;

typedef struct memory_block_node_tag
{
    int address;
    int block_size;
    char var_name[NAME_SIZE];
    struct memory_block_node_tag *next;
}memory_block;

//Global lists
memory_block *free_list,*allocated_list;

memory_block *makenode(memory_block b)
{
    memory_block *bptr;
    bptr=(memory_block *)malloc(sizeof(memory_block));
    *bptr=b;
    return bptr;
}

void initialise_memory()
{
    memory_block b;
    b.address=0;
    b.block_size=MEMORY_SIZE;
    b.var_name[0]='\0';
    b.next=NULL;
    free_list=makenode(b);
    
    allocated_list=NULL;
}

void print_free_block(memory_block *fptr)
{
    if(fptr!=NULL)
    {
        printf("{\n");
        printf("  %08d  FREE \n",fptr->address);
        printf("  ////////////\n");
        printf("  %08d\n",fptr->address+fptr->block_size-1);
        printf("}\n");
    }
}

void print_active_block(memory_block *aptr)
{
    if(aptr!=NULL)
    {
        printf("{\n");
        printf("  %08d  USED \n",aptr->address);
        printf("  Variable %s (%d bites)\n",aptr->var_name,aptr->block_size);
        printf("  %08d\n",aptr->address+aptr->block_size-1);
        printf("}\n");
    }
}

void merge_free_list()
{
    memory_block *current_ptr=free_list;
    if(free_list!=NULL)
    {
        memory_block *next_ptr=free_list->next;
        while(next_ptr!=NULL)
        {
            if(current_ptr->address + current_ptr->block_size == next_ptr->address)
            {
                (current_ptr->block_size)+=next_ptr->block_size;
                current_ptr->next=next_ptr->next;
                free(next_ptr);
                next_ptr=current_ptr->next;
            }
            else
            {
                current_ptr=next_ptr;
                next_ptr=next_ptr->next;
            }
        }
    }
}

void my_insert(memory_block *free_block)
{
    memory_block *current_ptr=free_list,*prev_ptr=NULL;
    status status_code=FAILURE;
    if(free_list==NULL || (free_list->address) > (free_block->address))
    {
        //Inserting at start
        free_block->next=free_list;
        free_list=free_block;
        status_code=SUCCESS;
    }
    while(current_ptr!=NULL && !status_code)
    {
        if((current_ptr->address) > (free_block->address))
        {
            prev_ptr->next=free_block;
            free_block->next=current_ptr;
            free_block->var_name[0]='\0';
            status_code=SUCCESS;
        }
        prev_ptr=current_ptr;
        current_ptr=current_ptr->next;
    }
    if(!status_code)
    {
        //Inserting at end
        prev_ptr->next=free_block;
        free_block->next=NULL;
        free_block->var_name[0]='\0';
    }
    merge_free_list();
}

void free_variable(char name[])
{
    memory_block *current_ptr=allocated_list,*prev_ptr=NULL;
    status status_code=FAILURE;
    if(strcmp(allocated_list->var_name,name)==0)
    {
        allocated_list=allocated_list->next;
        current_ptr->next=NULL;
        my_insert(current_ptr);
        status_code=SUCCESS;
    }
    while(current_ptr!=NULL && !status_code)
    {
        if(strcmp(current_ptr->var_name,name)==0)
        {
            prev_ptr->next=current_ptr->next;
            current_ptr->next=NULL;
            my_insert(current_ptr);
            status_code=SUCCESS;
        }
        prev_ptr=current_ptr;
        current_ptr=current_ptr->next;
    }
    if(!status_code)
    {
        printf("Variable %s not found in the heap\n",name);
    }
}

memory_block *first_fit(int size) 
{
    memory_block *return_ptr=NULL;
    memory_block *current_ptr=free_list, *prev_ptr = NULL;

    while (current_ptr != NULL) {
        //Case 1: Perfect fit
        if (current_ptr->block_size == size) {
            if (prev_ptr == NULL) {
                free_list = current_ptr->next;
            } else {
                prev_ptr->next = current_ptr->next;
            }
            current_ptr->next = NULL; // Isolate the allocated block
            return_ptr = current_ptr;
            break;
        }
        //Case 2: Free block is larger than required
        else if (current_ptr->block_size > size) {
            memory_block temp;
            temp.block_size = size;
            temp.address = current_ptr->address;
            temp.next = NULL;
            temp.var_name[0] = '\0';

            return_ptr = makenode(temp); // Create a new node for allocation

            // Reduce the current free block size
            current_ptr->address += size;
            current_ptr->block_size -= size;
            break;
        }
        prev_ptr = current_ptr;
        current_ptr = current_ptr->next;
    }

    return return_ptr;
}

memory_block *best_fit(int size) 
{
    memory_block *return_ptr=NULL;
    memory_block *current_ptr=free_list,*prev_ptr=NULL;
    memory_block *best_prev = NULL, *best_block = NULL;
    //Traverse free_list to find the best fitting block
    while(current_ptr!=NULL) 
    {
        if(current_ptr->block_size >= size) 
        {
            if(best_block==NULL || current_ptr->block_size < best_block->block_size) 
            {
                best_block=current_ptr;
                best_prev=prev_ptr;
            }
        }
        prev_ptr=current_ptr;
        current_ptr=current_ptr->next;
    }
    //If a suitable block is found, allocate it
    if (best_block!=NULL) 
    {
        if(best_block->block_size==size) 
        { 
            //Perfect fit,remove from free list
            if(best_prev==NULL) 
            {
                free_list=best_block->next;
            } 
            else 
            {
                best_prev->next=best_block->next;
            }
            best_block->next=NULL;
            return_ptr=best_block;
        } 
        else if(best_block->block_size > size) 
        {
            //Split the block
            memory_block temp;
            temp.block_size=size;
            temp.address=best_block->address;
            temp.next=NULL;
            temp.var_name[0]='\0';
            return_ptr=makenode(temp); 
            //Reduce the free block size
            best_block->address+=size;
            best_block->block_size-=size;
        }
    }
    return return_ptr;
}


void insert_active_list(memory_block *active_block)
{
    memory_block *current_ptr=allocated_list,*prev_ptr=NULL;
    status status_code=FAILURE;
    if(allocated_list==NULL || (allocated_list->address) > (active_block->address))
    {
        //Inserting at start
        active_block->next=allocated_list;
        allocated_list=active_block;
        status_code=SUCCESS;
    }
    while(current_ptr!=NULL && !status_code)
    {
        if((current_ptr->address) > (active_block->address))
        {
            prev_ptr->next=active_block;
            active_block->next=current_ptr;
            status_code=SUCCESS;
        }
        prev_ptr=current_ptr;
        current_ptr=current_ptr->next;
    }
    if(!status_code)
    {
        //Inserting at end
        prev_ptr->next=active_block;
        active_block->next=NULL;
    }
}

void allocate()
{
    char name[NAME_SIZE];
    memory_block *block;
    int size;
    boolean flag;
    status status_code=FAILURE;
    printf("Enter variable name: ");
    scanf("%s",name);
    printf("Size of variable (in bytes): ");
    scanf("%d",&size);
    if(name[0]!='\0' && size>0)
    {
        printf("0.Best fit    1.First fit\n");
        printf("Enter choice:");
        scanf("%d",&flag);
        if(!flag)
        {
            block=best_fit(size);
        }
        else
        {
            block=first_fit(size);
        }
        if(block!=NULL)
        {
            block->block_size=size;
            strcpy(block->var_name,name);
            block->next=NULL;
            insert_active_list(block);
            status_code=SUCCESS;
        }
        else
        {
            printf("No enough size in the heap\n");
        }
    }

    if(!status_code)
    {
        printf("Allocation failed\n");
    }

}

void print_memory()
{
    memory_block *aptr,*fptr;
    aptr=allocated_list;
    fptr=free_list;
    while(aptr!=NULL && fptr!=NULL)
    {
        if(aptr->address < fptr->address)
        {
            print_active_block(aptr);
            aptr=aptr->next;
        }
        else
        {
            print_free_block(fptr);
            fptr=fptr->next;
        }  
    }
    if(aptr==NULL)
    {
        while(fptr!=NULL)
        {
            print_free_block(fptr);
            fptr=fptr->next;
        }
    }
    else
    {
        while(aptr!=NULL)
        {
            print_active_block(aptr);
            aptr=aptr->next;
        }
    }
}

void cleanup_memory()
{
    memory_block *temp;
    while(allocated_list!=NULL)
    {
        temp=allocated_list;
        allocated_list=allocated_list->next;
        free(temp);
    }
    while(free_list!=NULL)
    {
        temp=free_list;
        free_list=free_list->next;
        free(temp);
    }
}


int main()
{
    int choice;
    boolean flag=TRUE;
    char n[NAME_SIZE];
    initialise_memory();
    while(flag)
    {
        printf("0.Exit\n");
        printf("1.Allocate memory\n");
        printf("2.Free memory\n");
        printf("3.Display heap\n");
        printf("Enter choice:");
        scanf("%d",&choice);
        switch(choice)
        {
            case 0:flag=FALSE;
            break;
            case 1:allocate();
            break;
            case 2:
            if(allocated_list!=NULL)
            {
                printf("Enter variable name to free it:");
                scanf("%s",n);
                free_variable(n);
            }
            else
            {
                printf("Allocated list is empty\n");
            }
            break;
            case 3:print_memory();
            break;
            default : printf("Invalid input\n");
            break;
        }
    }
    cleanup_memory();
    return 0;

}
