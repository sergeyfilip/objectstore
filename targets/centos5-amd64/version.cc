// Auto generated
#include "version.hh"
const char *g_getVersion() { return "1.1.12-manual"; }
const char *g_getArchitecture() { return "centos5-amd64"; }
uint32_t g_getBuild() { return 0; }
namespace global_config {
 const char *capath;
}
extern const char *g_getCAFile() { return "/etc/pki/tls/certs/ca-bundle.crt"; }
extern const char *g_getCAPath() { return global_config::capath; }
