#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
//#include <sys/wait.h>
#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#define BLOCK_SIZE 1024
#define MAX_FILE_SIZE 4194304 // 4GB of file size
#define FREE_ARRAY_SIZE 251 // free and inode array size
#define INODE_SIZE 64

/*************** super block structure**********************/
typedef struct {
        unsigned int isize; // 4 byte
        unsigned int fsize;
        unsigned int nfree;
        unsigned short free[FREE_ARRAY_SIZE];
        unsigned short flock;
        unsigned short ilock;
        unsigned short fmod;
        unsigned int time[2];
} super_block;

/****************inode structure ************************/
typedef struct {
        unsigned short flags; // 2 bytes
        char nlinks;  // 1 byte
        char uid;
        char gid;
        unsigned int size; // 32bits  2^32 = 4GB filesize
        unsigned short addr[9]; // to make total size = 64 byte inode size
        unsigned int actime;
        unsigned int modtime;
} Inode;


typedef struct
{
        unsigned short inode;
        char filename[28];
}dEntry;


super_block super;
int fd;
char pwd[100];
int curINodeNumber;
char fileSystemPath[100];
int total_inodes_count;
unsigned int ninode;
unsigned short inode[FREE_ARRAY_SIZE];

void writeToBlock (int blockNumber, void * buffer, int nbytes)
{
        lseek(fd,BLOCK_SIZE * blockNumber, SEEK_SET);
        write(fd,buffer,nbytes);
}


void addFreeBlock(int blockNumber){
        if(super.nfree == FREE_ARRAY_SIZE)
        {
                //write to the new block
                writeToBlock(blockNumber, super.free, FREE_ARRAY_SIZE * 2);
                super.nfree = 0;
        }
        super.free[super.nfree] = blockNumber;
        super.nfree++;
}

Inode getInode(int INumber){
        Inode iNode;
        int blockNumber =  2 ; //(INumber * INODE_SIZE) / BLOCK_SIZE;    // need to remove 
        int offset = ((INumber - 1) * INODE_SIZE) % BLOCK_SIZE;
        lseek(fd,(BLOCK_SIZE * blockNumber) + offset, SEEK_SET);
        read(fd,&iNode,INODE_SIZE);
        return iNode;
}

int getFreeBlock(){
        if(super.nfree == 0){
                int blockNumber = super.free[0];
                lseek(fd,BLOCK_SIZE * blockNumber , SEEK_SET);
                read(fd,super.free, FREE_ARRAY_SIZE * 2);
                super.nfree = 100;
                return blockNumber;
        }
        super.nfree--;
        return super.free[super.nfree];
}

void writeToBlockOffset(int blockNumber, int offset, void * buffer, int nbytes)
{
        lseek(fd,(BLOCK_SIZE * blockNumber) + offset, SEEK_SET);
        write(fd,buffer,nbytes);
}

void addFreeInode(int iNumber){
        if(ninode == FREE_ARRAY_SIZE)
                return;
        inode[ninode] = iNumber;
        ninode++;
}
void writeInode(int INumber, Inode inode){
        int blockNumber =  2 ;//(INumber * INODE_SIZE )/ BLOCK_SIZE;   //need to remove
        int offset = ((INumber - 1) * INODE_SIZE) % BLOCK_SIZE;
        writeToBlockOffset(blockNumber, offset, &inode, sizeof(Inode));
}

void femptyInode(int INumber, Inode inode, int nbytes){
        int blockNumber =  2 ;//(INumber * INODE_SIZE )/ BLOCK_SIZE;   //need to remove
        int offset = ((INumber - 1) * INODE_SIZE) % BLOCK_SIZE;
        writeToBlockOffset(blockNumber, offset, &inode, nbytes);
}

void createRootDirectory(){
        int blockNumber = getFreeBlock();
        dEntry directory[2];
        directory[0].inode = 0;
        strcpy(directory[0].filename,".");

        directory[1].inode = 0;
        strcpy(directory[1].filename,"..");

        writeToBlock(blockNumber, directory, 2*sizeof(dEntry));
		printf("\n root contents are stored in one of the data blocks  \n");

        Inode root;
        root.flags = 1<<14 | 1<<15; // setting 14th and 15th bit to 1, 15: allocated and 14: directory
        root.nlinks = 1;
        root.uid = 0;
        root.gid = 0;
        root.size = 2*sizeof(dEntry);
        root.addr[0] = blockNumber;
        root.actime = time(NULL);
        root.modtime = time(NULL);

        writeInode(1,root);
		printf("\n root is allocated to inode number 1 \n");
        curINodeNumber = 0;
        strcpy(pwd,"/");
}

int openfs(const char *filename)
{
	//fd=open(filename,2);
		char *path = filename;
	        //create file for File System
        if((fd = open(path,O_RDWR|O_CREAT,0600))== -1)
        {
                printf("\n file opening error [%s]\n",strerror(errno));
                return;
        }else{
			printf("The open() system call is successfully executed. \n");
		}
		
		strcpy(fileSystemPath,path);
	lseek(fd,BLOCK_SIZE,SEEK_SET);
	read(fd,&super,sizeof(super));
	lseek(fd,2*BLOCK_SIZE,SEEK_SET);
        Inode root = getInode(1);
	read(fd,&root,sizeof(root));
	return 1;
}
void initfs( int total_blocks, int total_inodes)
{
        printf("\n filesystem intialization started \n");
        total_inodes_count = total_inodes;
        char emptyBlock[BLOCK_SIZE] = {0};
		Inode emptyInode;
		emptyInode.flags = 0<<15;
        int no_of_bytes,i,blockNumber,iNumber;

        //init isize (Number of blocks for inode)
        if(((total_inodes*INODE_SIZE)%BLOCK_SIZE) == 0) // 300*64 % 1024
                super.isize = (total_inodes*INODE_SIZE)/BLOCK_SIZE;
        else
                super.isize = (total_inodes*INODE_SIZE)/BLOCK_SIZE+1;

        //init fsize
        super.fsize = total_blocks;

        //create file for File System
        /*if((fd = open(path,O_RDWR|O_CREAT,0600))== -1)
        {
                printf("\n file opening error [%s]\n",strerror(errno));
                return;
        }
        strcpy(fileSystemPath,path); */

        writeToBlock(total_blocks-1,emptyBlock,BLOCK_SIZE); // writing empty block to last block

        // add all blocks to the free array
        super.nfree = 0;
        for (blockNumber= 1+super.isize; blockNumber< total_blocks; blockNumber++)
                addFreeBlock(blockNumber);
		printf("\n All data blocks are set free and added to free[] array \n");
        // add free Inodes to inode array
        ninode = 0;
        for (iNumber=1; iNumber < total_inodes ; iNumber++)
                addFreeInode(iNumber);


        super.flock = 'f';
        super.ilock = 'i';
        super.fmod = 'f';
        super.time[0] = 0;
        super.time[1] = 0;

        //write Super Block
        writeToBlock (1,&super,BLOCK_SIZE);
		printf("\n Super block is written to block number 1 \n");

        //allocate empty space for i-nodes
        for (i=1; i <= super.isize; i++)
               // writeToBlock(i,emptyBlock,BLOCK_SIZE);
			femptyInode(i, emptyInode,INODE_SIZE );
		printf("\n All inodes are unallocated \n");

        createRootDirectory();
		printf("\n filesystem intialization completed \n");
}

void quit()
{
        //close(fd);
		if ( close(fd) ){
		printf("The close() system call is successfully executed. \n");
		}else{
			printf("\n file closing error [%s]\n",strerror(errno));
		}
        exit(0);
}



int main(int argc, char *argv[])
{
        char c;

        printf("\n Clearing screen \n");
        system("clear");

        unsigned int blk_no =0, inode_no=0;
        char *fs_path;
        char *arg1, *arg2;
        char *my_argv, cmd[512];

        while(1)
        {
                printf("\n%s@%s>>>",fileSystemPath,pwd);
                scanf(" %[^\n]s", cmd);
                my_argv = strtok(cmd," ");
				if(strcmp(my_argv, "openfs")==0){
                        arg1 = strtok(NULL, " ");
						fs_path = arg1;
                        openfs(arg1);
                }else if(strcmp(my_argv, "initfs")==0)
                {

                        arg1 = strtok(NULL, " ");
                        arg2 = strtok(NULL, " ");
                        if(access(fs_path, X_OK) != -1)
                        {
                                printf("filesystem already exists. \n");
                                printf("same file system will be used\n");
                        }
                        else
                        {
                                if (!arg1 || !arg2)
                                        printf(" insufficient arguments to proceed\n");
                                else
                                {
                                        blk_no = atoi(arg1);
                                        inode_no = atoi(arg2);
                                        initfs(blk_no, inode_no);
                                }
                        }
                        my_argv = NULL;
                }else if(strcmp(my_argv, "q")==0){
                        quit();
                }
        }
}
