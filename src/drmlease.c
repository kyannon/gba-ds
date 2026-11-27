/* The second screen on a leased panel (drmlease.h).
 *
 * The lease request (wp_drm_lease_device_v1) is a C port of Azahar-Enhanced's
 * DrmLeaseClient (https://github.com/virtudude/azahar, branch drm-leasing,
 * commit d16299140, src/citra_qt/drm_lease/drm_lease_client.cpp, GPLv2 or
 * later), with its timeout on the first round trip from cerroverb's MHGU
 * Recomp port of it. connector_mode and panel_rotation are its
 * ReadConnectorMode (cerroverb/azahar, branch drm-leasing-link-crtc, commit
 * 71a132b3b) and ReadPanelOrientation (drm_lease_window.cpp), here on the
 * kernel's ioctls in place of libdrm's calls, the way libdrm's xf86drmMode.c
 * (MIT) makes them. The picture goes on the panel in two dumb buffers, the
 * first with the mode in one atomic commit (LinkConnectorToLeasedCrtc's
 * properties, the CRTC active and the leased plane added), then flips with
 * their events. Azahar's picture goes through Vulkan instead, so that part
 * is new here.
 *
 * libwayland-client is opened at run time (only on the systems that lease),
 * the way SDL does it (src/video/wayland/SDL_waylanddyn.h, zlib): the
 * macros below make the protocol headers' inline functions call through
 * pointers. No libdrm: the kernel's own headers have all it takes. */
#include "drmlease.h"

#ifdef GBADS_DRM_LEASE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>

#include "wayland-client-core.h"

#define WL_FUNCS(X)                                                                                                     \
	X(wl_display_connect) X(wl_display_disconnect) X(wl_display_get_fd) X(wl_display_flush) X(wl_display_prepare_read) \
	X(wl_display_cancel_read) X(wl_display_read_events) X(wl_display_dispatch_pending) X(wl_proxy_marshal_flags)        \
	X(wl_proxy_add_listener) X(wl_proxy_destroy) X(wl_proxy_get_version)
#define X(name) static __typeof__(&name) dl_##name;
WL_FUNCS(X)
#undef X
static const struct wl_interface* dl_wl_registry_interface;
static const struct wl_interface* dl_wl_callback_interface;
#define wl_display_connect (*dl_wl_display_connect)
#define wl_display_disconnect (*dl_wl_display_disconnect)
#define wl_display_get_fd (*dl_wl_display_get_fd)
#define wl_display_flush (*dl_wl_display_flush)
#define wl_display_prepare_read (*dl_wl_display_prepare_read)
#define wl_display_cancel_read (*dl_wl_display_cancel_read)
#define wl_display_read_events (*dl_wl_display_read_events)
#define wl_display_dispatch_pending (*dl_wl_display_dispatch_pending)
#define wl_proxy_marshal_flags (*dl_wl_proxy_marshal_flags)
#define wl_proxy_add_listener (*dl_wl_proxy_add_listener)
#define wl_proxy_destroy (*dl_wl_proxy_destroy)
#define wl_proxy_get_version (*dl_wl_proxy_get_version)
/* (the headers' extern declarations of wl_registry_interface and
 * wl_callback_interface become declarations of the pointers above) */
#define wl_registry_interface (*dl_wl_registry_interface)
#define wl_callback_interface (*dl_wl_callback_interface)
#include "wayland-client-protocol.h"
#include "drm-lease-v1-client-protocol.h"

#define LOG(...) fprintf(stderr, "drm lease: " __VA_ARGS__)

static bool wl_open(void) {
	static int opened = -1;
	if (opened >= 0) return opened;
	void* lib = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_LOCAL);
	opened = lib != NULL;
	if (!lib) {
		LOG("can't open libwayland-client: %s\n", dlerror());
		return false;
	}
#define X(name)                                                       \
	if (!(*(void**) &dl_##name = dlsym(lib, #name))) {                \
		LOG("libwayland-client has no %s\n", #name);                  \
		opened = 0;                                                   \
	}
	WL_FUNCS(X)
#undef X
	dl_wl_registry_interface = dlsym(lib, "wl_registry_interface");
	dl_wl_callback_interface = dlsym(lib, "wl_callback_interface");
	if (!dl_wl_registry_interface || !dl_wl_callback_interface) {
		LOG("libwayland-client has no wl_registry_interface / wl_callback_interface\n");
		opened = 0;
	}
	return opened;
}

/* ---------------------------------------------------------- the lease */

#define MAX_OFFERS 8
typedef struct {
	struct wp_drm_lease_connector_v1* handle;
	char name[32];
	uint32_t connectorId;
	bool done;
} Offer;

typedef struct {
	uint32_t handle, fb, pitch;
	uint64_t size;
	uint8_t* pixels;
} Buffer;

static struct {
	struct wl_display* display;
	struct wl_registry* registry;
	struct wp_drm_lease_device_v1* device;
	struct wp_drm_lease_v1* lease;
	Offer offers[MAX_OFFERS];
	int nOffers;
	int enumFd, fd;
	bool synced, deviceDone, finished, pumpDead;
	uint32_t connector, crtc, plane;
	struct drm_mode_modeinfo mode;
	int rotation;
	Buffer buf[2];
	int shown; /* the buffer on the panel (or flipping to it) */
	bool flipPending, dirty, lost;
	/* tests (GBADS_DRM_LEASE_TEST): the panel in memory */
	uint32_t* testPanel;
	char testPng[256];
	/* for the log */
	int copies, frames;
	double copyMs;
} L = { .enumFd = -1, .fd = -1 };

static Offer* find_offer(struct wp_drm_lease_connector_v1* c) {
	for (int i = 0; i < L.nOffers; ++i) {
		if (L.offers[i].handle == c) return &L.offers[i];
	}
	return NULL;
}

static void registry_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
	(void) data, (void) version;
	if (strcmp(interface, wp_drm_lease_device_v1_interface.name)) return;
	/* one global per DRM device: only the first is used */
	if (L.device) {
		LOG("ignoring another lease device\n");
		return;
	}
	L.device = wl_registry_bind(registry, name, &wp_drm_lease_device_v1_interface, 1u);
}
static void registry_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
	(void) data, (void) registry, (void) name;
}
static const struct wl_registry_listener registryListener = { registry_global, registry_global_remove };

static void device_drm_fd(void* data, struct wp_drm_lease_device_v1* d, int32_t fd) {
	(void) data, (void) d;
	L.enumFd = fd; /* (only for enumerating: the mode is read through the lease) */
}
static const struct wp_drm_lease_connector_v1_listener connectorListener;
static void device_connector(void* data, struct wp_drm_lease_device_v1* d, struct wp_drm_lease_connector_v1* c) {
	(void) d;
	if (L.nOffers == MAX_OFFERS) {
		wp_drm_lease_connector_v1_destroy(c);
		return;
	}
	L.offers[L.nOffers++] = (Offer) { .handle = c };
	wp_drm_lease_connector_v1_add_listener(c, &connectorListener, data);
}
static void device_done(void* data, struct wp_drm_lease_device_v1* d) {
	(void) data, (void) d;
	L.deviceDone = true;
}
static void device_released(void* data, struct wp_drm_lease_device_v1* d) {
	(void) data, (void) d;
}
static const struct wp_drm_lease_device_v1_listener deviceListener = { device_drm_fd, device_connector, device_done, device_released };

static void connector_name(void* data, struct wp_drm_lease_connector_v1* c, const char* name) {
	(void) data;
	Offer* o = find_offer(c);
	if (o) snprintf(o->name, sizeof(o->name), "%s", name);
}
static void connector_description(void* data, struct wp_drm_lease_connector_v1* c, const char* description) {
	(void) data, (void) c, (void) description;
}
static void connector_id(void* data, struct wp_drm_lease_connector_v1* c, uint32_t id) {
	(void) data;
	Offer* o = find_offer(c);
	if (o) o->connectorId = id;
}
static void connector_done(void* data, struct wp_drm_lease_connector_v1* c) {
	(void) data;
	Offer* o = find_offer(c);
	if (o) o->done = true;
}
static void connector_withdrawn(void* data, struct wp_drm_lease_connector_v1* c) {
	(void) data;
	Offer* o = find_offer(c);
	if (o) o->done = false, o->connectorId = 0;
}
static const struct wp_drm_lease_connector_v1_listener connectorListener = { connector_name, connector_description, connector_id,
	                                                                           connector_done, connector_withdrawn };

static void lease_fd(void* data, struct wp_drm_lease_v1* lease, int32_t fd) {
	(void) data, (void) lease;
	L.fd = fd;
}
static void lease_finished(void* data, struct wp_drm_lease_v1* lease) {
	(void) data, (void) lease;
	L.finished = true;
	LOG("the compositor ended the lease\n");
}
static const struct wp_drm_lease_v1_listener leaseListener = { lease_fd, lease_finished };

static void sync_done(void* data, struct wl_callback* cb, uint32_t serial) {
	(void) data, (void) cb, (void) serial;
	L.synced = true;
}
static const struct wl_callback_listener syncListener = { sync_done };

static bool is_synced(void) { return L.synced; }
static bool is_enumerated(void) { return L.deviceDone; }
static bool is_answered(void) { return L.fd >= 0 || L.finished; }

static double now_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

/* Reads the compositor's events until cond() holds or timeoutMs is up: the
 * final cond(), false also when the connection fails (DispatchUntil). */
static bool dispatch_until(bool (*cond)(void), int timeoutMs) {
	double deadline = now_ms() + timeoutMs;
	while (!cond()) {
		while (wl_display_prepare_read(L.display) != 0) {
			if (wl_display_dispatch_pending(L.display) < 0) return false;
		}
		if (cond()) { /* (the queue just read may have done it) */
			wl_display_cancel_read(L.display);
			return true;
		}
		short events = POLLIN;
		if (wl_display_flush(L.display) < 0) {
			if (errno != EAGAIN) {
				wl_display_cancel_read(L.display);
				return false;
			}
			events |= POLLOUT; /* the request still buffered: wait until it can go */
		}
		double left = deadline - now_ms();
		if (left <= 0) {
			wl_display_cancel_read(L.display);
			return cond();
		}
		struct pollfd p = { wl_display_get_fd(L.display), events, 0 };
		int r = poll(&p, 1, left < 1 ? 1 : (int) left);
		if ((r < 0 && errno != EINTR) || (p.revents & (POLLERR | POLLHUP | POLLNVAL))) {
			wl_display_cancel_read(L.display);
			return false;
		}
		if (r > 0 && (p.revents & POLLIN)) {
			if (wl_display_read_events(L.display) < 0) return false;
		} else {
			wl_display_cancel_read(L.display);
		}
		if (wl_display_dispatch_pending(L.display) < 0) return false;
	}
	return true;
}

/* The compositor's events, without waiting (PumpEvents): a finished lease. */
static void pump_events(void) {
	if (!L.display || L.pumpDead) return;
	bool ok = true;
	while (ok && wl_display_prepare_read(L.display) != 0) ok = wl_display_dispatch_pending(L.display) >= 0;
	if (ok) {
		struct pollfd p = { wl_display_get_fd(L.display), POLLIN, 0 };
		bool flushed = wl_display_flush(L.display) >= 0 || errno == EAGAIN;
		int r = flushed ? poll(&p, 1, 0) : -1;
		if (!flushed || (p.revents & (POLLERR | POLLHUP | POLLNVAL)) || (r < 0 && errno != EINTR)) {
			wl_display_cancel_read(L.display);
			ok = false;
		} else if (r > 0 && (p.revents & POLLIN)) {
			ok = wl_display_read_events(L.display) >= 0;
		} else {
			wl_display_cancel_read(L.display);
		}
		if (ok) ok = wl_display_dispatch_pending(L.display) >= 0;
	}
	if (!ok) {
		LOG("the connection to the compositor failed\n");
		L.pumpDead = true;
	}
}

static bool is_internal(const char* name) {
	static const char* prefixes[] = { "DSI-", "eDP-", "LVDS-", "DPI-" };
	for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
		if (!strncmp(name, prefixes[i], strlen(prefixes[i]))) return true;
	}
	return false;
}

/* Leases connector (or with "auto" the first internal panel offered): L.fd
 * and L.connector (Acquire). */
static bool request_lease(const char* want) {
	const char* gs = getenv("GAMESCOPE_WAYLAND_DISPLAY");
	/* gamescope hides WAYLAND_DISPLAY from games and names itself here: an
	 * outer compositor reached by default may not lease */
	if (gs) L.display = wl_display_connect(gs);
	if (!L.display) L.display = wl_display_connect(NULL);
	if (!L.display) {
		LOG("no Wayland display (GAMESCOPE_WAYLAND_DISPLAY %s)\n", gs ? gs : "unset");
		return false;
	}
	L.registry = wl_display_get_registry(L.display);
	wl_registry_add_listener(L.registry, &registryListener, NULL);
	/* (a sync with a timeout: wl_display_roundtrip waits for ever on a stalled compositor) */
	struct wl_callback* sync = wl_display_sync(L.display);
	wl_callback_add_listener(sync, &syncListener, NULL);
	bool answered = dispatch_until(is_synced, 5000);
	wl_callback_destroy(sync);
	if (!answered) {
		LOG("no answer from the compositor\n");
		return false;
	}
	if (!L.device) {
		LOG("the compositor (%s) leases nothing (no wp_drm_lease_device_v1)\n", gs ? gs : "WAYLAND_DISPLAY");
		return false;
	}
	wp_drm_lease_device_v1_add_listener(L.device, &deviceListener, NULL);
	/* the connectors may come well after the bind (while the compositor gets the display back) */
	if (!dispatch_until(is_enumerated, 5000)) {
		LOG("the compositor never finished listing its connectors\n");
		return false;
	}
	bool any = !strcmp(want, "auto");
	const Offer* chosen = NULL;
	for (int i = 0; i < L.nOffers && !chosen; ++i) {
		const Offer* o = &L.offers[i];
		LOG("offered: %s (id %u)%s\n", o->name, o->connectorId, o->done ? "" : ", withdrawn");
		if (o->done && (any ? is_internal(o->name) : !strcmp(o->name, want))) chosen = o;
	}
	if (!chosen) {
		LOG("no connector to lease (wanted %s, %d offered)\n", any ? "any internal panel" : want, L.nOffers);
		return false;
	}
	/* (copied before more events come: a withdrawal clears the offer, not a granted lease) */
	L.connector = chosen->connectorId;
	LOG("asking for %s (id %u)\n", chosen->name, L.connector);
	struct wp_drm_lease_request_v1* req = wp_drm_lease_device_v1_create_lease_request(L.device);
	wp_drm_lease_request_v1_request_connector(req, chosen->handle);
	L.lease = wp_drm_lease_request_v1_submit(req);
	wp_drm_lease_v1_add_listener(L.lease, &leaseListener, NULL);
	if (!dispatch_until(is_answered, 5000)) {
		LOG("no answer to the lease request\n");
		return false;
	}
	if (L.finished || L.fd < 0) {
		LOG("the compositor turned the lease request down\n");
		return false;
	}
	return true;
}

static void lease_disconnect(void) {
	if (L.lease) wp_drm_lease_v1_destroy(L.lease);
	for (int i = 0; i < L.nOffers; ++i) wp_drm_lease_connector_v1_destroy(L.offers[i].handle);
	if (L.device) {
		/* no round trip: after a timeout the compositor may be stalled, and
		 * the disconnect lets go of everything anyway */
		wp_drm_lease_device_v1_release(L.device);
		wl_display_flush(L.display);
		wp_drm_lease_device_v1_destroy(L.device);
	}
	if (L.registry) wl_registry_destroy(L.registry);
	if (L.display) wl_display_disconnect(L.display);
	if (L.enumFd >= 0) close(L.enumFd);
	L.lease = NULL, L.device = NULL, L.registry = NULL, L.display = NULL, L.nOffers = 0, L.enumFd = -1;
}

/* ---------------------------------------------------------- the panel */

/* (drmIoctl: again while interrupted) */
static int drm_ioctl(unsigned long request, void* arg) {
	int r;
	do r = ioctl(L.fd, request, arg);
	while (r == -1 && (errno == EINTR || errno == EAGAIN));
	return r;
}

/* The connector's preferred mode, else its first (ReadConnectorMode). */
static bool connector_mode(struct drm_mode_modeinfo* out) {
	struct drm_mode_get_connector c = { .connector_id = L.connector };
	if (drm_ioctl(DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0 || c.count_modes == 0) return false;
	struct drm_mode_modeinfo* modes = calloc(c.count_modes, sizeof(*modes));
	if (!modes) return false;
	/* (only the modes: libdrm's drmModeGetConnector fetches the rest too) */
	struct drm_mode_get_connector c2 = { .connector_id = L.connector, .count_modes = c.count_modes,
		                                 .modes_ptr = (uint64_t) (uintptr_t) modes };
	bool ok = drm_ioctl(DRM_IOCTL_MODE_GETCONNECTOR, &c2) == 0 && c2.count_modes > 0;
	if (ok) {
		uint32_t n = c2.count_modes < c.count_modes ? c2.count_modes : c.count_modes;
		*out = modes[0];
		for (uint32_t i = 0; i < n; ++i) {
			if (modes[i].type & DRM_MODE_TYPE_PREFERRED) {
				*out = modes[i];
				break;
			}
		}
	}
	free(modes);
	return ok;
}

/* An object's properties: their ids and values (n of them, the caller
 * frees both), as drmModeObjectGetProperties fetches them. */
static uint32_t obj_properties(uint32_t obj, uint32_t type, uint32_t** ids, uint64_t** values) {
	struct drm_mode_obj_get_properties op = { .obj_id = obj, .obj_type = type };
	*ids = NULL, *values = NULL;
	if (drm_ioctl(DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op) < 0 || !op.count_props) return 0;
	uint32_t n = op.count_props;
	*ids = calloc(n, sizeof(**ids));
	*values = calloc(n, sizeof(**values));
	op.props_ptr = (uint64_t) (uintptr_t) *ids, op.prop_values_ptr = (uint64_t) (uintptr_t) *values;
	if (!*ids || !*values || drm_ioctl(DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op) < 0) return 0;
	return op.count_props < n ? op.count_props : n;
}

/* An object's property by name: its id, 0 when it has none (FindPropertyId). */
static uint32_t find_property(uint32_t obj, uint32_t type, const char* name) {
	uint32_t *ids, found = 0;
	uint64_t* values;
	uint32_t n = obj_properties(obj, type, &ids, &values);
	for (uint32_t i = 0; i < n && !found; ++i) {
		struct drm_mode_get_property p = { .prop_id = ids[i] };
		if (drm_ioctl(DRM_IOCTL_MODE_GETPROPERTY, &p) == 0 && !strcmp(p.name, name)) found = ids[i];
	}
	free(ids);
	free(values);
	return found;
}

/* The connector's "panel orientation", in DRM's counter-clockwise degrees,
 * -1 when it has none (ReadPanelOrientation). */
static int panel_rotation(void) {
	uint32_t* ids;
	uint64_t* values;
	uint32_t n = obj_properties(L.connector, DRM_MODE_OBJECT_CONNECTOR, &ids, &values);
	int rotation = -1;
	for (uint32_t i = 0; i < n && rotation < 0; ++i) {
		struct drm_mode_get_property p = { .prop_id = ids[i] };
		if (drm_ioctl(DRM_IOCTL_MODE_GETPROPERTY, &p) < 0 || strcmp(p.name, "panel orientation") || !p.count_enum_blobs) continue;
		struct drm_mode_property_enum* enums = calloc(p.count_enum_blobs, sizeof(*enums));
		if (!enums) break;
		struct drm_mode_get_property p2 = { .prop_id = ids[i], .count_enum_blobs = p.count_enum_blobs,
			                                .enum_blob_ptr = (uint64_t) (uintptr_t) enums };
		if (drm_ioctl(DRM_IOCTL_MODE_GETPROPERTY, &p2) == 0) {
			uint32_t ne = p2.count_enum_blobs < p.count_enum_blobs ? p2.count_enum_blobs : p.count_enum_blobs;
			for (uint32_t j = 0; j < ne; ++j) {
				if (enums[j].value != values[i]) continue;
				const char* e = enums[j].name;
				rotation = !strcmp(e, "Normal") ? 0 : !strcmp(e, "Upside Down") ? 180 : !strcmp(e, "Left Side Up") ? 90
				         : !strcmp(e, "Right Side Up") ? 270 : -1;
				break;
			}
		}
		free(enums);
	}
	free(ids);
	free(values);
	return rotation;
}

/* The lease's CRTC: it holds one, the first of its resources (as
 * LinkConnectorToLeasedCrtc takes it). */
static uint32_t leased_crtc(void) {
	struct drm_mode_card_res r = { 0 };
	if (drm_ioctl(DRM_IOCTL_MODE_GETRESOURCES, &r) < 0 || !r.count_crtcs) return 0;
	uint32_t* crtcs = calloc(r.count_crtcs, sizeof(*crtcs));
	if (!crtcs) return 0;
	struct drm_mode_card_res r2 = { .count_crtcs = r.count_crtcs, .crtc_id_ptr = (uint64_t) (uintptr_t) crtcs };
	uint32_t id = drm_ioctl(DRM_IOCTL_MODE_GETRESOURCES, &r2) == 0 && r2.count_crtcs ? crtcs[0] : 0;
	free(crtcs);
	return id;
}

/* The lease's plane that can show on its CRTC (the first of the lease's
 * CRTCs, bit 0): gamescope leases one with the connector, and it need not
 * be the CRTC's legacy primary plane (MHGU Recomp's lease notes), so the
 * picture goes through atomic commits on this plane. */
static uint32_t leased_plane(void) {
	struct drm_mode_get_plane_res r = { 0 };
	if (drm_ioctl(DRM_IOCTL_MODE_GETPLANERESOURCES, &r) < 0 || !r.count_planes) return 0;
	uint32_t* planes = calloc(r.count_planes, sizeof(*planes));
	if (!planes) return 0;
	struct drm_mode_get_plane_res r2 = { .count_planes = r.count_planes, .plane_id_ptr = (uint64_t) (uintptr_t) planes };
	uint32_t id = 0;
	if (drm_ioctl(DRM_IOCTL_MODE_GETPLANERESOURCES, &r2) == 0) {
		uint32_t n = r2.count_planes < r.count_planes ? r2.count_planes : r.count_planes;
		for (uint32_t i = 0; i < n && !id; ++i) {
			struct drm_mode_get_plane gp = { .plane_id = planes[i] };
			if (drm_ioctl(DRM_IOCTL_MODE_GETPLANE, &gp) == 0 && (gp.possible_crtcs & 1)) id = planes[i];
		}
	}
	free(planes);
	return id;
}

/* An atomic request: properties added object by object. */
#define ATOMIC_MAX 16
typedef struct {
	uint32_t objs[ATOMIC_MAX], counts[ATOMIC_MAX], props[ATOMIC_MAX];
	uint64_t values[ATOMIC_MAX];
	int nObjs, nProps;
} Atomic;

static void atomic_add(Atomic* a, uint32_t obj, uint32_t prop, uint64_t value) {
	if (a->nProps == ATOMIC_MAX) return;
	if (!a->nObjs || a->objs[a->nObjs - 1] != obj) a->objs[a->nObjs] = obj, a->counts[a->nObjs++] = 0;
	++a->counts[a->nObjs - 1];
	a->props[a->nProps] = prop, a->values[a->nProps++] = value;
}

static int atomic_commit(const Atomic* a, uint32_t flags) {
	struct drm_mode_atomic req = { .flags = flags, .count_objs = a->nObjs, .objs_ptr = (uint64_t) (uintptr_t) a->objs,
		                           .count_props_ptr = (uint64_t) (uintptr_t) a->counts, .props_ptr = (uint64_t) (uintptr_t) a->props,
		                           .prop_values_ptr = (uint64_t) (uintptr_t) a->values };
	return drm_ioctl(DRM_IOCTL_MODE_ATOMIC, &req);
}

static bool buffer_make(Buffer* b, uint32_t w, uint32_t h) {
	struct drm_mode_create_dumb cd = { .width = w, .height = h, .bpp = 32 };
	if (drm_ioctl(DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) {
		LOG("can't make a %ux%u buffer: %s\n", w, h, strerror(errno));
		return false;
	}
	b->handle = cd.handle, b->pitch = cd.pitch, b->size = cd.size;
	struct drm_mode_fb_cmd fc = { .width = w, .height = h, .pitch = cd.pitch, .bpp = 32, .depth = 24, .handle = cd.handle };
	struct drm_mode_map_dumb md = { .handle = cd.handle };
	if (drm_ioctl(DRM_IOCTL_MODE_ADDFB, &fc) < 0 || drm_ioctl(DRM_IOCTL_MODE_MAP_DUMB, &md) < 0) {
		LOG("can't set up a buffer: %s\n", strerror(errno));
		return false;
	}
	b->fb = fc.fb_id;
	void* p = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, L.fd, (off_t) md.offset);
	if (p == MAP_FAILED) {
		LOG("can't map a buffer: %s\n", strerror(errno));
		return false;
	}
	b->pixels = p;
	memset(b->pixels, 0, cd.size); /* black */
	return true;
}

static void buffer_free(Buffer* b) {
	if (b->pixels) munmap(b->pixels, b->size);
	if (b->fb) drm_ioctl(DRM_IOCTL_MODE_RMFB, &b->fb);
	if (b->handle) {
		struct drm_mode_destroy_dumb dd = { .handle = b->handle };
		drm_ioctl(DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
	}
	*b = (Buffer) { 0 };
}

/* The leased objects' properties that the commits set. */
static struct {
	uint32_t connCrtc, crtcMode, crtcActive, fb, crtc, srcX, srcY, srcW, srcH, crtcX, crtcY, crtcW, crtcH;
} P;

static bool find_properties(void) {
	P.connCrtc = find_property(L.connector, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
	P.crtcMode = find_property(L.crtc, DRM_MODE_OBJECT_CRTC, "MODE_ID");
	P.crtcActive = find_property(L.crtc, DRM_MODE_OBJECT_CRTC, "ACTIVE");
	static const char* names[] = { "FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H", "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H" };
	uint32_t* plane[] = { &P.fb, &P.crtc, &P.srcX, &P.srcY, &P.srcW, &P.srcH, &P.crtcX, &P.crtcY, &P.crtcW, &P.crtcH };
	bool ok = P.connCrtc && P.crtcMode && P.crtcActive;
	for (int i = 0; i < 10; ++i) ok = (*plane[i] = find_property(L.plane, DRM_MODE_OBJECT_PLANE, names[i])) && ok;
	return ok;
}

/* The plane showing buffer b over the whole panel. */
static void atomic_add_plane(Atomic* a, const Buffer* b) {
	atomic_add(a, L.plane, P.fb, b->fb);
	atomic_add(a, L.plane, P.crtc, L.crtc);
	atomic_add(a, L.plane, P.srcX, 0);
	atomic_add(a, L.plane, P.srcY, 0);
	atomic_add(a, L.plane, P.srcW, (uint64_t) L.mode.hdisplay << 16);
	atomic_add(a, L.plane, P.srcH, (uint64_t) L.mode.vdisplay << 16);
	atomic_add(a, L.plane, P.crtcX, 0);
	atomic_add(a, L.plane, P.crtcY, 0);
	atomic_add(a, L.plane, P.crtcW, L.mode.hdisplay);
	atomic_add(a, L.plane, P.crtcH, L.mode.vdisplay);
}

/* The mode, both buffers, and the first (black) on the panel, in one
 * commit: the connector linked to the CRTC with the mode, as
 * LinkConnectorToLeasedCrtc does, but active, with the plane. */
static bool panel_light(void) {
	if (!connector_mode(&L.mode)) {
		LOG("the leased connector has no mode\n");
		return false;
	}
	struct drm_set_client_cap cap = { .capability = DRM_CLIENT_CAP_ATOMIC, .value = 1 };
	if (drm_ioctl(DRM_IOCTL_SET_CLIENT_CAP, &cap) < 0) {
		LOG("no atomic mode setting: %s\n", strerror(errno));
		return false;
	}
	L.crtc = leased_crtc();
	L.plane = leased_plane();
	if (!L.crtc || !L.plane || !find_properties()) {
		LOG("the lease's objects are missing (CRTC %u, plane %u) or lack properties\n", L.crtc, L.plane);
		return false;
	}
	if (!buffer_make(&L.buf[0], L.mode.hdisplay, L.mode.vdisplay) || !buffer_make(&L.buf[1], L.mode.hdisplay, L.mode.vdisplay)) return false;
	struct drm_mode_create_blob blob = { .data = (uint64_t) (uintptr_t) &L.mode, .length = sizeof(L.mode) };
	if (drm_ioctl(DRM_IOCTL_MODE_CREATEPROPBLOB, &blob) < 0) {
		LOG("can't make the mode blob: %s\n", strerror(errno));
		return false;
	}
	Atomic a = { 0 };
	atomic_add(&a, L.connector, P.connCrtc, L.crtc);
	atomic_add(&a, L.crtc, P.crtcMode, blob.blob_id);
	atomic_add(&a, L.crtc, P.crtcActive, 1);
	atomic_add_plane(&a, &L.buf[0]);
	int r = atomic_commit(&a, DRM_MODE_ATOMIC_ALLOW_MODESET);
	int err = errno;
	struct drm_mode_destroy_blob db = { .blob_id = blob.blob_id };
	drm_ioctl(DRM_IOCTL_MODE_DESTROYPROPBLOB, &db);
	if (r < 0) {
		LOG("can't set the mode %s on CRTC %u, plane %u: %s\n", L.mode.name, L.crtc, L.plane, strerror(err));
		return false;
	}
	L.shown = 0;
	return true;
}

/* The panel's pixel (x, y) shows the upright picture's (u, v), for the
 * panel's rotation (MHGU Recomp's put(), checked on the Thor, 270): as x
 * and y go up, the picture's point moves by stepX and stepY pixels. */
static void rotation_steps(int pw, int ph, int stride, long* origin, long* stepX, long* stepY) {
	switch (L.rotation) {
	case 90: *origin = ph - 1, *stepX = stride, *stepY = -1; break;                     /* u = ph-1-y, v = x */
	case 180: *origin = (long) (ph - 1) * stride + pw - 1, *stepX = -1, *stepY = -stride; break; /* u = pw-1-x, v = ph-1-y */
	case 270: *origin = (long) (pw - 1) * stride, *stepX = -stride, *stepY = 1; break; /* u = y, v = pw-1-x */
	default: *origin = 0, *stepX = 1, *stepY = stride; break;
	}
}

/* The upright picture into a panel-sized buffer, turned, in tiles: each
 * tile reads a few rows of the picture and writes whole short runs of the
 * panel's rows (the mapped buffer is write-combined: written in order). */
static void copy_turned(uint32_t* dst, int dstStride, int pw, int ph, const uint32_t* src, int srcStride) {
	enum { TILE = 32 };
	long origin, stepX, stepY;
	rotation_steps(pw, ph, srcStride, &origin, &stepX, &stepY);
	for (int ty = 0; ty < ph; ty += TILE) {
		int yEnd = ty + TILE < ph ? ty + TILE : ph;
		for (int tx = 0; tx < pw; tx += TILE) {
			int xEnd = tx + TILE < pw ? tx + TILE : pw;
			for (int y = ty; y < yEnd; ++y) {
				const uint32_t* s = src + origin + stepY * y + stepX * tx;
				uint32_t* d = dst + (long) y * dstStride + tx;
				for (int x = tx; x < xEnd; ++x, s += stepX) *d++ = *s | 0xff000000u;
			}
		}
	}
}

/* The panel's flip events: the flip done, the other buffer free again. */
static void read_flip_events(void) {
	struct pollfd p = { L.fd, POLLIN, 0 };
	while (L.flipPending && poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) {
		char evs[1024];
		ssize_t n = read(L.fd, evs, sizeof(evs));
		if (n <= 0) break;
		for (ssize_t i = 0; i + (ssize_t) sizeof(struct drm_event) <= n;) {
			struct drm_event* e = (struct drm_event*) (evs + i);
			if (e->type == DRM_EVENT_FLIP_COMPLETE) L.flipPending = false;
			if (e->length < sizeof(struct drm_event)) break;
			i += e->length;
		}
	}
}

static void lost(const char* why) {
	if (!L.lost) LOG("the panel is gone (%s): the second screen is no longer shown\n", why);
	L.lost = true;
}

/* ---------------------------------------------------------- drmlease.h */

static int upright_w(void) { return L.rotation == 90 || L.rotation == 270 ? L.mode.vdisplay : L.mode.hdisplay; }
static int upright_h(void) { return L.rotation == 90 || L.rotation == 270 ? L.mode.hdisplay : L.mode.vdisplay; }

static bool open_test(const char* spec, int* w, int* h) {
	int pw = 0, ph = 0, rot = 0, at = 0;
	if (sscanf(spec, "%dx%d,%d%n", &pw, &ph, &rot, &at) < 3 || pw <= 0 || ph <= 0) {
		LOG("GBADS_DRM_LEASE_TEST=%s: wanted WxH,ROTATION[,PNG]\n", spec);
		return false;
	}
	if (spec[at] == ',') snprintf(L.testPng, sizeof(L.testPng), "%s", spec + at + 1);
	L.mode.hdisplay = pw, L.mode.vdisplay = ph, L.rotation = rot;
	L.testPanel = calloc((size_t) pw * ph, 4);
	if (!L.testPanel) return false;
	*w = upright_w(), *h = upright_h();
	LOG("test panel %dx%d in memory, rotation %d: picture %dx%d\n", pw, ph, rot, *w, *h);
	return true;
}

bool drmlease_open(int* w, int* h) {
	const char* test = getenv("GBADS_DRM_LEASE_TEST");
	if (test) return open_test(test, w, h);
	const char* want = getenv("GBADS_DRM_LEASE");
	if (!want || !*want) return false;
	if (!wl_open() || !request_lease(want) || !panel_light()) {
		drmlease_close();
		LOG("not leased: one window only\n");
		return false;
	}
	int r = panel_rotation();
	L.rotation = r < 0 ? 0 : r;
	*w = upright_w(), *h = upright_h();
	LOG("connector %u on CRTC %u, plane %u, %s (%ux%u), panel orientation %d%s: picture %dx%d\n", L.connector, L.crtc, L.plane, L.mode.name,
	    L.mode.hdisplay, L.mode.vdisplay, L.rotation, r < 0 ? " (none given)" : "", *w, *h);
	return true;
}

int drmlease_rotation(void) {
	return L.rotation;
}

void drmlease_present(SDL_Surface* s, bool changed) {
	L.dirty |= changed;
	++L.frames;
	if (L.testPanel) {
		if (L.dirty) {
			double t0 = now_ms();
			copy_turned(L.testPanel, L.mode.hdisplay, L.mode.hdisplay, L.mode.vdisplay, s->pixels, s->pitch / 4);
			L.copyMs += now_ms() - t0, ++L.copies, L.dirty = false;
		}
	} else if (!L.lost) {
		pump_events();
		if (L.finished || L.pumpDead) lost(L.finished ? "lease ended" : "compositor connection failed");
		read_flip_events();
		if (!L.lost && L.dirty && !L.flipPending) {
			int next = 1 - L.shown;
			Buffer* b = &L.buf[next];
			double t0 = now_ms();
			copy_turned((uint32_t*) b->pixels, b->pitch / 4, L.mode.hdisplay, L.mode.vdisplay, s->pixels, s->pitch / 4);
			L.copyMs += now_ms() - t0, ++L.copies;
			Atomic a = { 0 };
			atomic_add(&a, L.plane, P.fb, b->fb);
			if (atomic_commit(&a, DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK) == 0) {
				L.shown = next, L.flipPending = true, L.dirty = false;
			} else if (errno != EBUSY) {
				char why[64];
				snprintf(why, sizeof(why), "flip: %s", strerror(errno));
				lost(why);
			}
		}
	}
	if (L.frames == 600) {
		LOG("%d of %d frames put on the panel, %.2f ms per copy\n", L.copies, L.frames, L.copies ? L.copyMs / L.copies : 0.0);
		L.frames = L.copies = 0, L.copyMs = 0;
	}
}

/* stb_image_write is compiled in logo.c */
int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

void drmlease_close(void) {
	if (L.testPanel) {
		if (L.testPng[0]) {
			/* XRGB8888 (B, G, R, X in memory) as RGBA */
			size_t n = (size_t) L.mode.hdisplay * L.mode.vdisplay;
			for (size_t i = 0; i < n; ++i) {
				uint32_t p = L.testPanel[i];
				L.testPanel[i] = 0xff000000u | (p & 0xff00u) | (p >> 16 & 0xffu) | (p & 0xffu) << 16;
			}
			bool ok = stbi_write_png(L.testPng, L.mode.hdisplay, L.mode.vdisplay, 4, L.testPanel, L.mode.hdisplay * 4);
			LOG("test panel -> %s: %s\n", L.testPng, ok ? "ok" : "failed");
		}
		free(L.testPanel);
		L.testPanel = NULL;
		return;
	}
	for (int i = 0; i < 2; ++i) buffer_free(&L.buf[i]);
	if (L.display) lease_disconnect();
	/* closing the lease gives the panel back (gamescope blanks it and its
	 * bottom-screen companion takes it again) */
	if (L.fd >= 0) close(L.fd);
	L.fd = -1;
}

#endif
