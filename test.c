/*
 * =====================================================================================
 *
 *       Filename:  test.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  11/17/2019 15:41:50
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Junha Hyung (), sharpeeee@kaist.ac.kr
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

int main(){
    int a = sbrk(10);
    if(a==-1){
        printf("error\n");
    }
    else{
        printf("success\n");
    }
    return 0;
}

