#pragma once

typedef unsigned short __uint16;
typedef unsigned  __uint32;
typedef unsigned long long __uint64;
typedef short __int16;
typedef int __int32;
typedef long long __int64;

#include <stdio.h>
#define ERROR(fmt,args...) printf(fmt"\n",##args)

#pragma pack()