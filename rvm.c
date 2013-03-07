#include <stdio.h>
#include <stdlib.h>
#include "rvm.h"
#include <sys/stat.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

typedef struct seg_map_struct{
  char seg_name[100];
	void *addr;
	int size;
	int valid;
	char full_name_log[200];
	char full_name[200];
}seg_map_t;

typedef struct trans_map_struct{
	int numsegs;
	void **addr;
	int valid;
}trans_map_t;

seg_map_t seg_map[200];
trans_map_t trans_map[200];

trans_t trans_id=0;
int num_mapped = 0;

void flush_log(char *full_name, char *full_name_log);

//initialize with specific directory
rvm_t rvm_init(const char * directory)						
{
	struct stat st;
	if(stat(directory, &st) != 0)
	{
		int status;
		status = mkdir(directory, 0777);
		if(status)
		{
			printf("Error in opening file. Program is exiting.\n");
			exit(1);
		}
	}
	rvm_t rvm;
	strcpy(rvm.dir, directory);
	strcat(rvm.dir, "/");

	return rvm;
}


void *rvm_map(rvm_t rvm, const char *segname, int size_to_create)
{
	char full_name[200], full_name_log[200];

	strcpy(full_name, rvm.dir);
	strcat(full_name, segname);
	strcpy(full_name_log, full_name);
	strcat(full_name_log, ".log");

	struct stat st;
	FILE *seg, *log;

	int i;
	for(i=0; i<num_mapped; i++)
	{
		if(!strcmp(seg_map[i].seg_name, segname))
		{
			printf("Segment already mapped. Program is exiting.\n");
			exit(1);
		}
	}

	void *mem_seg = calloc(1, size_to_create);

	strcpy(seg_map[num_mapped].seg_name, segname);
	seg_map[num_mapped].addr = mem_seg;
	seg_map[num_mapped].valid = 1;
	strcpy(seg_map[num_mapped].full_name_log, full_name_log);
	strcpy(seg_map[num_mapped].full_name, full_name);
	seg_map[num_mapped].size = size_to_create;

	if(stat(full_name_log, &st) != 0)
	{
		log = fopen(full_name_log, "wb");
		if(!log)
		{
			printf("Error in creating file. Program is exiting.\n");
			exit(1);
		}
		if(log!=NULL)
			fclose(log);
	}
	else{
		flush_log(seg_map[num_mapped].full_name, seg_map[num_mapped].full_name_log);
	}

	if(stat(full_name, &st) == 0) //exist then load
	{
		int size;
		size = st.st_size;
		seg = fopen(full_name, "rb");
		fread(mem_seg, 1, size_to_create, seg);
		if(size < size_to_create)
		{
			freopen(full_name, "wb", seg);
			fwrite(mem_seg, 1, size_to_create, seg);
		}
		fclose(seg);
	}
	else
	{
		seg = fopen(full_name, "wb");
		fwrite(mem_seg, 1, size_to_create, seg);
		if(seg!=NULL)
			fclose(seg);
	}
	
	num_mapped++;

	return mem_seg;
}

void rvm_unmap(rvm_t rvm, void *segbase)
{
	char fseg_name[200];
									//what if in a transaction TODO
//check if segment mapped
	int i;
	for(i=0; i<num_mapped; i++)
	{
		if(seg_map[i].addr == segbase)
		{
			//do writting for the log file and the delete log file

			//log file writting function here.	//TODO
			flush_log(seg_map[i].full_name, seg_map[i].full_name_log);

			remove(seg_map[i].full_name_log);

			seg_map[i].addr = NULL;
			strcpy(seg_map[i].seg_name, "\0");
			seg_map[i].valid = 0;
			strcpy(seg_map[i].full_name_log, "\0");
			strcpy(seg_map[i].full_name, "\0");

			free(segbase);
			return;
		}
	}

	printf("Segment not mapped. Program is exiting.\n");
	exit(1);
	return;
}

void rvm_destroy(rvm_t rvm, const char *segname)	
{
	char full_name[200];
	strcpy(full_name, rvm.dir);
	strcat(full_name, segname);

//check if segment mapped
	int i;
	for(i=0; i<num_mapped; i++)
	{
		if(!strcmp(seg_map[i].seg_name, segname))
		{
			printf("Segment mapped. Program is exiting.\n");
			exit(1);
		}
	}
	
//delete the segment file
	remove(full_name);
	strcat(full_name,".log");
	remove(full_name);

	return;
}

trans_t rvm_begin_trans(rvm_t rvm, int numsegs,void **segbases)		
{
	trans_t tid = -1;
//check if a segment exist in another transaction
	int i, j, k;
	for(i=0; i<numsegs; i++)
	{
		for(j=0; j<trans_id; j++)
		{
			for(k=0; k<trans_map[j].numsegs; k++)
			{
				if(segbases[i] == trans_map[j].addr[k])
				{
					return -1;
				}
			}
		}
	}
	trans_map[trans_id].numsegs = numsegs;
	trans_map[trans_id].addr = segbases;
	trans_map[trans_id].valid = 1;

	tid = trans_id;
	trans_id++;

	return tid;
}

void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size)	
{
	int i,j;
	for(i=0; i<trans_map[tid].numsegs; i++)
	{
		if(trans_map[tid].addr[i] == segbase)
		{
			for(j=0; j<num_mapped; j++)
			{
				if(seg_map[j].addr == segbase)
				{
					FILE *log;
					
					log = fopen(seg_map[i].full_name_log, "ab");

					fprintf(log, "\n--CHECKPOINT--\n");
					fprintf(log, "%d\n%d\n", offset, size);
					fwrite((segbase+offset), 1, size, log);
					fclose(log);
					return;
				}
			}
		}
	}
	printf("Requested segment not in transaction. Program is exiting.\n");
	exit(1);
}

void rvm_commit_trans(trans_t tid)						
{
	int i,j, offset, size;
	long int truncpos, lsize, rsize;
	FILE *log, *temp_log;
	struct stat st;
	void *temp_mem;
	char *checkpoint="--CHECKPOINT--", *commit="--COMMIT--", temp_file[200], temp[1000];

	for(i=0; i<trans_map[tid].numsegs; i++)
	{
		for(j=0; j<num_mapped; j++)
		{
			if(trans_map[tid].addr[i] == seg_map[j].addr)
			{
				log = fopen(seg_map[j].full_name_log, "rb");
				truncpos = ftell(log);
				while(log!=NULL && fgets(temp, sizeof(temp), log)!=NULL)
				{
					if(strstr(temp, commit))
					{
						truncpos = ftell(log);

					}
				}

				strcpy(temp_file, seg_map[j].full_name_log);
				strcat(temp_file, ".tmp");
				temp_log = fopen(temp_file, "wb");

				fprintf(temp_log, "\n--COMMIT START--\n");
				fseek(log, truncpos, SEEK_SET);
				while((log!=NULL) && (fgets(temp, sizeof(temp), log)!=NULL))
				{
					if(strstr(temp, checkpoint))
					{
						fscanf(log, "%d\n%d\n", &offset, &size);
						fprintf(temp_log, "\n--CHECKPOINT--\n");
						fprintf(temp_log, "%d\n%d\n", offset, size);
						fwrite((seg_map[j].addr+offset), 1, size, temp_log);
					}
				}
				fprintf(temp_log, "\n--COMMIT--\n");
				fclose(temp_log);
				stat(temp_file, &st);
				lsize = st.st_size;
				rsize = lsize + truncpos;
				temp_mem = calloc(1, rsize);

				temp_log = fopen(temp_file, "rb");
				rewind(log);
				fread(temp_mem, 1, truncpos, log);
				fread((temp_mem + truncpos), 1, lsize, temp_log);
				freopen(seg_map[j].full_name_log, "wb", log);
				fwrite(temp_mem, 1, rsize, log);

				fclose(log);
				fclose(temp_log);
				remove(temp_file);
				free(temp_mem);
				break;
			}
		}
	}
	trans_map[tid].numsegs = 0;
	trans_map[tid].addr = NULL;
	trans_map[tid].valid = 0;

	return;
}

void rvm_commit_trans_heavy(trans_t tid)
{
	int i, j, len;
	FILE *log, *seg;
	long int truncpos, size_seg, extra;
	char temp[1000];
	char *commit = "--COMMIT--", *checkpoint = "--CHECKPOINT--";
	struct stat st;
	int offset, size;
	void *mem_seg, *temp_mem;

	for(i=0; i<trans_map[tid].numsegs; i++)
	{
		for(j=0; j<num_mapped; j++)
		{
			if(trans_map[tid].addr[i] == seg_map[j].addr)
			{
				stat(seg_map[i].full_name, &st);
				size_seg = st.st_size;

				extra = size_seg - seg_map[i].size;
				if(extra)
				{
					temp_mem = calloc(1, extra);

					seg = fopen(seg_map[i].full_name, "rb");
					fseek(seg, seg_map[i].size, SEEK_SET);
					fread(temp_mem, 1, extra, seg);
					fclose(seg);
				}
				seg = fopen(seg_map[i].full_name, "wb");
				fwrite(trans_map[tid].addr[i], 1, seg_map[i].size, seg);
				if(extra)
				{
					freopen(seg_map[i].full_name, "ab", seg);
					fwrite(temp_mem, 1, extra, seg);
					free(temp_mem);
				}
				fclose(seg);

				log = fopen(seg_map[i].full_name_log, "rb");
				truncpos = ftell(log);
				while(log!=NULL && fgets(temp, sizeof(temp), log)!=NULL)
				{
					if(strstr(temp, commit))
					{
						truncpos = ftell(log);

					}
				}

				rewind(log);
				temp_mem = calloc(1, truncpos);
				fread(temp_mem, 1, truncpos, log);
				freopen(seg_map[i].full_name_log, "wb", log);
				fwrite(temp_mem, 1, truncpos, log);
				if(log!=NULL)
					fclose(log);
				break;
			}
		}
	}
	trans_map[tid].numsegs = 0;
	trans_map[tid].addr = NULL;
	trans_map[tid].valid = 0;
	return;
}

void rvm_abort_trans(trans_t tid)
{
	int i, j, offset, size;
	FILE *log;
	long int truncpos;
	char temp[1000];
	char *commit = "--COMMIT--", *checkpoint = "--CHECKPOINT--";
	void *temp_mem;

	for(i=0; i<trans_map[tid].numsegs; i++)
	{
		for(j=0; j<num_mapped; j++)
		{
			if(trans_map[tid].addr[i] == seg_map[j].addr)
			{
				log = fopen(seg_map[i].full_name_log, "rb");
				truncpos = ftell(log);
				while(log!=NULL && fgets(temp, sizeof(temp), log)!=NULL)
				{
					if(strstr(temp, commit))
					{
						truncpos = ftell(log);

					}
				}

				fseek(log, truncpos, SEEK_SET);
				while(log!=NULL && fgets(temp, sizeof(temp), log)!=NULL)
				{
					if(strstr(temp, checkpoint))
					{
						fscanf(log, "%d\n%d\n", &offset, &size);
						fread((seg_map[j].addr+offset), 1, size, log);
					}	
				}

				rewind(log);
				temp_mem = calloc(1, truncpos);
				fread(temp_mem, 1, truncpos, log);
				freopen(seg_map[i].full_name_log, "wb", log);
				fwrite(temp_mem, 1, truncpos, log);
				if(log!=NULL)
					fclose(log);
				break;
			}
		}
	}
	trans_map[tid].numsegs = 0;
	trans_map[tid].addr = NULL;
	trans_map[tid].valid = 0;
	return;
}

void rvm_truncate_log(rvm_t rvm)
{
	DIR *dirp;
	struct dirent *dp;
	char full_name[200], full_name_log[200];
	int len;

	dirp = opendir(rvm.dir);
	while((dp = readdir(dirp))!=NULL)
	{
		if(strstr(dp->d_name, ".log"))
		{
			strcpy(full_name_log, rvm.dir);
			strcat(full_name_log, dp->d_name);
			len = strlen(full_name_log);
			strncpy(full_name, full_name_log, (len-4));
			strcat(full_name, "\0");

			flush_log(full_name, full_name_log);	
		}
	}
	(void)closedir(dirp);
//			flush_log(i);			//to use
}

void flush_log(char *full_name, char *full_name_log)
{
	int len, offset, size;
	FILE *log, *seg, *rem;
	char temp[1000];
	struct stat st;
	void *mem_seg, *temp_mem;
	char *checkpoint = "--CHECKPOINT--", *commit = "--COMMIT--", rem_file[200];
	long int truncpos, temppos, size_seg, log_size, new_log_size;

	stat(full_name, &st);
	size_seg = st.st_size;				//get size of seg
	mem_seg = calloc(1, size_seg);
	seg = fopen(full_name, "rb");
	fread(mem_seg, 1, size_seg, seg);		//get the disk seg to memory.

	stat(full_name_log, &st);
	log_size = st.st_size;

	log = fopen(full_name_log, "rb");
				
	truncpos = ftell(log);
	temppos = truncpos;
	while(log!=NULL && fgets(temp, sizeof(temp), log)!=NULL)
	{
		if(strstr(temp, commit))
		{
			truncpos = ftell(log);
		}
	}

	strcpy(rem_file, full_name_log);
	strcat(rem_file, ".tmp");
	rem = fopen(rem_file, "wb");
	rewind(log);
	temp_mem = calloc(1, truncpos);
	fread(temp_mem, 1, truncpos, log);
	fwrite(temp_mem, 1, truncpos, rem);
	fclose(rem);
	free(temp_mem);

	new_log_size = log_size - truncpos;
	temp_mem = calloc(1, new_log_size);
	fseek(log, truncpos, SEEK_SET);
	fread(temp_mem, 1, new_log_size, log);
	freopen(full_name_log, "wb", log);
	fwrite(temp_mem, 1, new_log_size, log);
	fclose(log);
	free(temp_mem);

//working till here
	rem = fopen(rem_file, "rb");
	while((rem!=NULL) && fgets(temp, sizeof(temp), rem)!=NULL)
	{
		if(strstr(temp, checkpoint))
		{
			fscanf(log, "%d\n%d\n", &offset, &size);
			fread((mem_seg+offset), 1, size, rem);
		}
	}
	fclose(rem);

//write the updated memory segment
	freopen(full_name, "wb", seg);
	fwrite(mem_seg, 1, size_seg, seg);
	if(seg!=NULL)
		fclose(seg);
	remove(rem_file);	
	return;
}
