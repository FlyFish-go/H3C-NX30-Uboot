/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NX30_D_ENV_CORE_H__
#define __NX30_D_ENV_CORE_H__
#ifdef NX30_D_ENV_HOST_TEST
#include <stdint.h>
#include <stddef.h>
typedef uint8_t nx30_d_env_u8;
#else
#include <linux/types.h>
#include <linux/stddef.h>
typedef u8 nx30_d_env_u8;
#endif
#ifdef __UBOOT__
#include <linux/string.h>
#else
#include <string.h>
#endif
static inline int nx30_d_env_core_rewrite(
 const nx30_d_env_u8 *input,nx30_d_env_u8 *output,
 size_t header_size,size_t data_size,const char * const *allow_names,
 size_t allow_count,const char * const *change_names,
 const char * const *change_values,const unsigned char *change_unset,
 size_t change_count,size_t value_capacity,char *snapshot_values)
{
 const nx30_d_env_u8 *data; nx30_d_env_u8 *out;
 size_t pos=0,outpos=0,tail_start=0,i,j;
 unsigned char seen[8]={0};
 if (!input || !output || !allow_names || allow_count>8 ||
     change_count>allow_count || !value_capacity || !snapshot_values ||
     (change_count && (!change_names || !change_values || !change_unset))) return -1;
 data=input+header_size; out=output+header_size;
 memset(output,0,header_size+data_size);
 memset(snapshot_values,0,allow_count*value_capacity);
 while (pos<data_size) {
  size_t end=pos,entry_len,key_len; int index=-1,remove_entry=0;
  if (data[pos]==0) {
   if (pos==0) {
    if (pos+1>=data_size || data[pos+1]!=0) return -1;
    tail_start=pos+2;
   } else {
    tail_start=pos+1;
   }
   break;
  }
  while (end<data_size && data[end]) end++;
  if (end==data_size) return -1;
  entry_len=end-pos;
  for (key_len=0;key_len<entry_len && data[pos+key_len]!='=';key_len++);
  if (!key_len || key_len==entry_len) return -1;
  for (i=0;i<allow_count;i++)
   if (strlen(allow_names[i])==key_len && !memcmp(data+pos,allow_names[i],key_len)) { index=i; break; }
  if (index>=0) {
   size_t value_len=entry_len-key_len-1;
   if (seen[index]++) return -2;
   if (value_len>=value_capacity) return -3;
   memcpy(snapshot_values+index*value_capacity,data+pos+key_len+1,value_len);
   for (j=0;j<change_count;j++) if (!strcmp(change_names[j],allow_names[index])) {
    remove_entry=1; break;
   }
   if (remove_entry) { pos=end+1; continue; }
  }
  if (outpos+entry_len+1>data_size-2) return -4;
  memcpy(out+outpos,data+pos,entry_len); outpos+=entry_len; out[outpos++]=0;
  pos=end+1;
 }
 if (!tail_start) return -1;
 for (j=0;j<change_count;j++) {
  size_t keylen=strlen(change_names[j]),vlen=strlen(change_values[j]);
  if (change_unset[j]) continue;
  if (outpos+keylen+1+vlen+1>data_size-2) return -4;
  memcpy(out+outpos,change_names[j],keylen); outpos+=keylen; out[outpos++]='=';
  memcpy(out+outpos,change_values[j],vlen); outpos+=vlen; out[outpos++]=0;
 }
 out[outpos++]=0; out[outpos++]=0;
 if (tail_start<data_size && outpos<data_size) {
  size_t tail=data_size-tail_start;
  if (tail>data_size-outpos) tail=data_size-outpos;
  memcpy(out+outpos,data+tail_start,tail);
 }
 return 0;
}
#endif
