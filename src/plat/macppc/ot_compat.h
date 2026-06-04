/*
 * Minimal Open Transport declarations for Retro68 / multiversal interfaces.
 *
 * The Retro68 toolchain ships PEF import stubs for OpenTransportLib and
 * OpenTptInternetLib, but the "multiversal" CIncludes set has no
 * <OpenTransport.h> / <OpenTransportProviders.h>. This header declares just
 * enough types + entry points for a synchronous/blocking TCP client.
 *
 * The system libraries export *Priv variants (InitOpenTransportPriv, ...);
 * the public InitOpenTransport/OTOpenEndpoint/etc. live in static glue
 * objects (OpenTransportAppPPC.o, OpenTptInetPPC.o) that own a per-app
 * client record and bring up ASLM before calling the Priv entry. Retro68
 * doesn't ship that glue, so we vendor it from MPW-GM (see
 * tools/fetch_otglue.sh + third_party/otglue/) and link it directly.
 * On PPC `pascal` is a no-op, so plain C decls are correct.
 */
#ifndef OT_COMPAT_H
#define OT_COMPAT_H

#include <MacTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef SInt32 OSStatus;
typedef SInt32 OTResult;
typedef UInt32 OTOpenFlags;
typedef UInt32 OTByteCount;
typedef UInt32 OTFlags;
typedef UInt16 OTAddressType;
typedef UInt32 InetHost;
typedef UInt16 InetPort;

typedef struct OTConfiguration *OTConfigurationRef;
typedef void *ProviderRef;
typedef void *EndpointRef;
typedef void *InetSvcRef;

struct TNetbuf {
    OTByteCount maxlen;
    OTByteCount len;
    UInt8 *buf;
};
typedef struct TNetbuf TNetbuf;

struct TBind {
    TNetbuf addr;
    UInt32 qlen;
};
typedef struct TBind TBind;

struct TCall {
    TNetbuf addr;
    TNetbuf opt;
    TNetbuf udata;
    UInt32 sequence;
};
typedef struct TCall TCall;

struct TDiscon {
    TNetbuf udata;
    SInt32 reason;
    UInt32 sequence;
};
typedef struct TDiscon TDiscon;

enum { AF_INET = 2 };
enum { kOTInvalidRef = 0 };

struct InetAddress {
    OTAddressType fAddressType;
    InetPort fPort;
    InetHost fHost;
    UInt8 fUnused[8];
};
typedef struct InetAddress InetAddress;

enum { kMaxHostAddrs = 10, kMaxHostNameLen = 255 };
struct InetHostInfo {
    char name[kMaxHostNameLen + 1];
    InetHost addrs[kMaxHostAddrs];
};
typedef struct InetHostInfo InetHostInfo;

#define kTCPName "tcp"
#define kDefaultInternetServicesPath ((OTConfigurationRef) - 3L)

enum { T_DISCONNECT = 0x0010, T_ORDREL = 0x0080 };

enum {
    kOTLookErr = -3158,
    kOTNoDataErr = -3162,
    kOTOutStateErr = -3155,
    kOTFlowErr = -3161
};

/* ---- Static glue (OpenTransportAppPPC.o) ---- */
extern OSStatus InitOpenTransport(void);
extern void CloseOpenTransport(void);
extern EndpointRef OTOpenEndpoint(OTConfigurationRef cfg, OTOpenFlags oflag,
                                  void *info, OSStatus *err);
extern OSStatus OTCloseProvider(ProviderRef ref);
/* ---- Static glue (OpenTptInetPPC.o) ---- */
extern InetSvcRef OTOpenInternetServices(OTConfigurationRef cfg,
                                         OTOpenFlags oflag, OSStatus *err);

/* ---- OpenTransportLib (PEF import) ---- */
extern OTConfigurationRef OTCreateConfiguration(const char *path);
extern OSStatus OTSetSynchronous(ProviderRef ref);
extern OSStatus OTSetBlocking(ProviderRef ref);
extern OSStatus OTSetNonBlocking(ProviderRef ref);
extern OSStatus OTBind(EndpointRef ref, TBind *req, TBind *ret);
extern OSStatus OTConnect(EndpointRef ref, TCall *snd, TCall *rcv);
extern OTResult OTSnd(EndpointRef ref, void *buf, OTByteCount nbytes,
                      OTFlags flags);
extern OTResult OTRcv(EndpointRef ref, void *buf, OTByteCount nbytes,
                      OTFlags *flags);
extern OTResult OTLook(EndpointRef ref);
extern OSStatus OTCountDataBytes(EndpointRef ref, OTByteCount *countPtr);
extern OSStatus OTRcvOrderlyDisconnect(EndpointRef ref);
extern OSStatus OTRcvDisconnect(EndpointRef ref, TDiscon *discon);
extern OSStatus OTSndOrderlyDisconnect(EndpointRef ref);
extern OSStatus OTUnbind(EndpointRef ref);

/* ---- OpenTptInternetLib (PEF import) ---- */
extern OSStatus OTInetStringToAddress(InetSvcRef ref, char *name,
                                      InetHostInfo *hinfo);
extern OSStatus OTInetStringToHost(const char *str, InetHost *host);
extern void OTInitInetAddress(InetAddress *addr, InetPort port, InetHost host);

/* ---- InterfaceLib: not in Multiverse.h ---- */
struct UnsignedWide {
    UInt32 hi;
    UInt32 lo;
};
typedef struct UnsignedWide UnsignedWide;
extern void Microseconds(UnsignedWide *us);

#ifdef __cplusplus
}
#endif
#endif /* OT_COMPAT_H */
