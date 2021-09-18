///
/// Common definitions for the main object storage server
///
//
/// $Id: main.hh,v 1.1 2013/08/27 13:30:54 joe Exp $
//

#ifndef OBJSTORE_MAIN_HH
#define OBJSTORE_MAIN_HH

//
/// In order not to have directories with huge numbers of child
/// objects, we split the name into components. Now, every bit in the
/// hash is mostly independent of any other bit, and we only use an
/// infinitesimally small amount of the full hash space. We will
/// probably hold a few billion objects in a 256-bit name space - this
/// amount to something like a utilisation of:
//
///   2^34   (16 billion objects)
///  -------                         = 2^(-222)  (near zero utilisation)
///   2^256  (size of hash name space)
//
/// Four billion objects can be represented at four levels using 8
/// bits at each level. However, our name is 256 bits.
//
/// We simply use 8 bits of the name at the first three levels, and
/// let the remaining 232 bits spill into the last level.
//
std::string splitName(const std::string &);


#endif
