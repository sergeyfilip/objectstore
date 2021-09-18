///
/// OpenSSL initialization code
///
//
// $Id: ssl.hh,v 1.1 2013/04/16 13:41:16 joe Exp $
//

#ifndef COMMON_SSL_HH
#define COMMON_SSL_HH

/// Call this before making any OpenSSL calls. This will initialise
/// the OpenSSL library, load the error strings and set up locking for
/// multi-threaded use.  This routine can be called any number of
/// times (from any number of threads - concurrently) - it will do
/// nothing after the first call.
void init_openssl_library();

#endif
