/*
 * =====================================================================================
 *
 *       Filename:  main.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  11/19/2019 16:33:35
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Junha Hyung (), sharpeeee@kaist.ac.kr
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>
#include "tcmalloc_api.h"

char testmem[1<<23];
void*perthread(void*x);

int main(){
    memset(testmem,'a',1<<23);
    void* pageheap = tc_central_init();
    if(pageheap == NULL){
        printf("central initialization failed\n");
        return 0;
    }
    else printf("central init success\n");

    pthread_t thread[1000];
    for (int i=0;i<1000;i++){
        if(pthread_create(&thread[i], NULL, perthread, NULL)){
            printf("failed pthread create\n");
            return 0;
        }
    }
    for(int j=0;j<1000;j++){
        if(pthread_join(thread[j],NULL)){
            printf("pthread join failed\n");
            return 0;
        }
    }
    printf("success\n");

}

void *perthread(void* x){
    void* threadcache = tc_thread_init();
    if(threadcache == NULL){
        printf("thread init failed\n");
        return NULL;
    }
    char*ptr1[100];
    char*ptr2[100];
    char*ptr3[10];
    for(int i=0;i<100;i++){
        ptr1[i] = (char*)tc_malloc(31);
        memset(ptr1[i],'a',31);
        ptr2[i] = (char*)tc_malloc(100);
        memset(ptr2[i],'b',100);
    }
    for(int i=0;i<10;i++){
        ptr3[i] = (char*)tc_malloc(64*1024);
        memset(ptr3[i],'c',64*1024);
    }
    // printing out first
    for(int i=0;i<100;i++){
        printf("ptr1: %s\n",ptr1[i]);
    }

    // printing out second
    /*  
    for(int i=0;i<100;i++){
        printf("ptr2: %s\n",ptr2[i]);
    }
    */
    //printf("ptr: %p\n",ptr);
    //printf("pid: %lx\n",(uintptr_t)ptr>>12);

    // test with a,b and assert
    /*
    if(ptr){
        memset(ptr,'a',16*1024);
        memset(ptr+16*1024,'b',16*1024);
    }
    for(int i=0;i<32*1024;i++){
        if(i<16*1024){
            assert(*(ptr+i) == 'a');
        }
        else {
            if(*(ptr+i) != 'b'){
                printf("(i=%d)what? = %c\n",i,*(ptr+i));
                for(int j=0;j<32*1024;j++){
                    printf("%c",*(ptr+j));
                }
                printf("\n");
            }
            assert(*(ptr+i)=='b');
        }
    }
    */
    /* 
    uint32_t pid = (uintptr_t)ptr>>12;
    for(int i=pid;i<pid+8;i++){
        if(testmem[i]!='a'){
            printf("error\n");
            printf("%c\n", testmem[i]);
            printf("i: %x\n",i);
            printf("pid: %x\n",pid);
            assert(testmem[i]=='a');
        }
        else testmem[i]='b';
    }
    */

    printf("perthread success\n");
    return NULL;
}
