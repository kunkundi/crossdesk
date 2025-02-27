#ifndef _RTCP_TYPEDEF_H_
#define _RTCP_TYPEDEF_H_

#include <cstddef>
#include <cstdint>

#define DEFAULT_RTCP_VERSION 2
#define DEFAULT_RTCP_HEADER_SIZE 4

#define DEFAULT_SR_BLOCK_NUM 1
#define DEFAULT_SR_SIZE 28
#define DEFAULT_RR_BLOCK_NUM 1
#define DEFAULT_RR_SIZE 32

typedef enum {
  UNKNOWN = 0,
  SR = 200,
  RR = 201,
  SDES = 202,
  BYE = 203,
  APP = 204
} RTCP_TYPE;

#endif