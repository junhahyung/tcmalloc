/*
 * =====================================================================================
 *
 *       Filename:  tcmalloc_api.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  11/19/2019 20:33:40
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

void *tc_central_init();
void *tc_thread_init();
void *tc_malloc(uint32_t size);
void tc_free(void *ptr);
