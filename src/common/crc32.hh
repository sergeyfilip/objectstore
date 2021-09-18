//
// A simple CRC-32 implementation
//
// $Id: crc32.hh,v 1.1 2012/12/19 14:51:23 joe Exp $
//

#ifndef COMMON_CRC32_HH
#define COMMON_CRC32_HH

#include <stdint.h>
#include <vector>

// Return a 32-bit CRC of the sequence
uint32_t crc32(const std::vector<uint8_t> &data);


#endif
