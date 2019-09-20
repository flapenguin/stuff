#define _GNU_SOURCE

#include <sys/syscall.h>
#include <asm-generic/ioctl.h>

#include <drm/drm.h>
#include <drm/i915_drm.h>

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <dlfcn.h>

#include <fnmatch.h>

#include "util.h"

static int (*original_ioctl)(int fd, unsigned long int request, ...);
static const char* pattern = 0;
__attribute__ ((constructor))
static void ioctl__initialize() {
  original_ioctl = dlsym(RTLD_NEXT, "ioctl");
  pattern = getenv("IOCTL_LOGGER") ?: "";
}

static const char* handle__prefix;
static int handle__indent = 0;
static int handle__skipped = false;

#define PREFIX ":: "

#define ORIGINAL_HANDLER() original_ioctl(fd, request, arg)
#define HANDLE(Request, HasResponse, Type, ...) do { \
  if (request != Request) break; \
  handle__skipped = fnmatch(pattern, #Request, 0) == FNM_NOMATCH; \
  if (handle__skipped) break; \
  Type* req = arg; (void)req; \
  fprintf(stderr, "\033[94m" PREFIX "\033[33mioctl(%d, " #Request ")\n", fd); \
  fputs("\033[94m", stderr); \
  handle__prefix = ""; \
  if (HasResponse) { \
    handle__indent = 2; \
    handle__prefix = "   "; \
  } \
   __VA_ARGS__ \
  int ret = ORIGINAL_HANDLER(); \
  fprintf(stderr, PREFIX "   (retval) = %d ", ret); \
  if (ret) fprintf(stderr, "(errno) = %d %s", errno, strerror(errno)); \
  fputc('\n', stderr); \
  if (HasResponse) { \
    handle__indent = 2; \
    handle__prefix = " =>"; \
    __VA_ARGS__ \
  } \
  fputs("\033[0m", stderr); \
  return ret; \
} while (0)

#define D__PREFIX() fprintf(stderr, PREFIX "%s%*s", handle__prefix, handle__indent, "");

/** Request Field shortcut  */
#define DRF(Field) DF(#Field, req->Field)

/** Object */
#define DO(Name, ...) do { D__PREFIX(); handle__indent += 2; fprintf(stderr, ".%s = {\n", Name); __VA_ARGS__; handle__indent -= 2; D__PREFIX(); fprintf(stderr, "}\n"); } while(0)
/** Object field */
#define DF(Name, ValueIdentifier) do { D__PREFIX(); fprintf(stderr, DUMPF(ValueIdentifier, ".%s = ", "\n"), Name, ValueIdentifier); } while(0)
/** Object custom field  */
#define DCF(Name, Format, ...) do { D__PREFIX(); fprintf(stderr, ".%s = " Format "\n", Name, __VA_ARGS__); } while(0)

/** Array  */
#define DA(Name, Length, ...) do { \
  D__PREFIX(); \
  handle__indent += 2; \
  fprintf(stderr, ".%s = (%zu elements) [\n", Name, (size_t)Length); \
  __VA_ARGS__; \
  handle__indent -= 2; \
  D__PREFIX(); \
  fprintf(stderr, "]\n"); \
} while(0)

/** Array element  */
#define DE(...) do { D__PREFIX(); handle__indent += 2; fprintf(stderr, "{\n"); __VA_ARGS__; handle__indent -= 2; D__PREFIX(); fprintf(stderr, "}\n"); } while(0)

static uint64_t d__flag;
#define DFLAGS(Name, Value, ...) do { \
  D__PREFIX(); \
  handle__indent += 2; \
  d__flag = Value; \
  fprintf(stderr, ".%s = 0x%lx\n", Name, d__flag); \
  handle__indent += 2; \
  __VA_ARGS__; \
  handle__indent -= 4; \
} while(0)

#define DFLAG(Flag) do { if (d__flag & Flag) { D__PREFIX(); fputs(#Flag "\n", stderr); } } while(0)
#define DCFLAG(Name) do { D__PREFIX(); fputs(Name "\n", stderr); } while(0)

#define DF_DOMAIN(Name, Value) DFLAGS(Name, Value, { \
  DFLAG(I915_GEM_DOMAIN_CPU); \
  DFLAG(I915_GEM_DOMAIN_RENDER); \
  DFLAG(I915_GEM_DOMAIN_SAMPLER); \
  DFLAG(I915_GEM_DOMAIN_COMMAND); \
  DFLAG(I915_GEM_DOMAIN_INSTRUCTION); \
  DFLAG(I915_GEM_DOMAIN_VERTEX); \
  DFLAG(I915_GEM_DOMAIN_GTT); \
  DFLAG(I915_GEM_DOMAIN_WC); \
})

// Satisfy -Werror=missing-prototypes.
int ioctl(int fd, unsigned long int request, void* arg);

// sys/ioctl.h has vararg signature with dots, but this is wierd flex of libc.
// https://stackoverflow.com/a/28467048
int ioctl(int fd, unsigned long int request, void* arg) {
  handle__skipped = false;

  HANDLE(DRM_IOCTL_VERSION, 0, struct drm_version, {});
  HANDLE(DRM_IOCTL_GEM_CLOSE, 0, struct drm_gem_close, {});
  HANDLE(DRM_IOCTL_PRIME_HANDLE_TO_FD, 0, struct drm_prime_handle, {});

  HANDLE(DRM_IOCTL_I915_GETPARAM, 0, struct drm_i915_getparam, {});
  HANDLE(DRM_IOCTL_I915_GEM_GET_APERTURE, 0, struct drm_i915_gem_get_aperture, {});
  HANDLE(DRM_IOCTL_I915_GEM_CONTEXT_CREATE, 0, struct drm_i915_gem_context_create, {});
  HANDLE(DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM, 0, struct drm_i915_gem_context_param, {});
  HANDLE(DRM_IOCTL_I915_REG_READ, 0, struct drm_i915_reg_read, {});
  HANDLE(DRM_IOCTL_I915_GET_RESET_STATS, 0, struct drm_i915_reset_stats, {});
  HANDLE(DRM_IOCTL_I915_GEM_PWRITE, 0, struct drm_i915_gem_pwrite, {});

  HANDLE(DRM_IOCTL_I915_GEM_CREATE, 1, struct drm_i915_gem_create, {
    DRF(size);
    DRF(handle);
  });

  HANDLE(DRM_IOCTL_I915_GEM_SET_DOMAIN, 0, struct drm_i915_gem_set_domain, {
    DRF(handle);
    DF_DOMAIN("read_domains", req->read_domains);
    DF_DOMAIN("write_domain", req->write_domain);
  });

  HANDLE(DRM_IOCTL_I915_GEM_MMAP, 1, struct drm_i915_gem_mmap, {
    DRF(handle);
    DCF("offset", "0x%llx", req->offset);
    DRF(size);
    DCF("addr_ptr", "0x%llx", req->addr_ptr);
    DRF(flags);
  });

  HANDLE(DRM_IOCTL_I915_GEM_SET_TILING, 1, struct drm_i915_gem_set_tiling, {
    DRF(handle);
    DRF(stride);
    DRF(tiling_mode);
    DRF(swizzle_mode);
  });

  HANDLE(DRM_IOCTL_I915_GEM_GET_TILING, 1, struct drm_i915_gem_get_tiling, {
    DRF(handle);
    DRF(tiling_mode);
    DRF(swizzle_mode);
    DRF(phys_swizzle_mode);
  });

  HANDLE(DRM_IOCTL_I915_GEM_BUSY, 0, struct drm_i915_gem_busy, {
    DRF(handle);
    DRF(busy);
  });

  #define ExecBufferFlagCase(X) case X: DCFLAG(#X); break
  HANDLE(DRM_IOCTL_I915_GEM_EXECBUFFER2, 1, struct drm_i915_gem_execbuffer2, {
    DA("execobjects", req->buffer_count, {
      const struct drm_i915_gem_exec_object2* exec_objects = (void*)req->buffers_ptr;
      for (uint32_t buffer_ix = 0; buffer_ix < req->buffer_count; buffer_ix++) DE({
        DF("handle", exec_objects[buffer_ix].handle);
        DCF("offset", "0x%llx", exec_objects[buffer_ix].offset);

        DFLAGS("flags", exec_objects[buffer_ix].flags, {
          DFLAG(EXEC_OBJECT_NEEDS_FENCE);
          DFLAG(EXEC_OBJECT_NEEDS_GTT);
          DFLAG(EXEC_OBJECT_WRITE);
          DFLAG(EXEC_OBJECT_SUPPORTS_48B_ADDRESS);
          DFLAG(EXEC_OBJECT_PINNED);
          DFLAG(EXEC_OBJECT_PAD_TO_SIZE);
          DFLAG(EXEC_OBJECT_ASYNC);
          DFLAG(EXEC_OBJECT_CAPTURE);
        });

        const struct drm_i915_gem_relocation_entry* relocations = (void*)exec_objects[buffer_ix].relocs_ptr;
        DA("relocations", exec_objects[buffer_ix].relocation_count, {
          for (uint32_t relocation_ix = 0; relocation_ix < exec_objects[buffer_ix].relocation_count; relocation_ix++) {
            DE({
              DF("target_handle", relocations[relocation_ix].target_handle);
              DF_DOMAIN("read_domains", relocations[relocation_ix].read_domains);
              DF_DOMAIN("write_domain", relocations[relocation_ix].write_domain);
              DF("offset", relocations[relocation_ix].offset);
            });
          }
        });
      });
    });

    DFLAGS("flags", req->flags, {
      switch (d__flag & 0x7) {
        ExecBufferFlagCase(I915_EXEC_RING_MASK);
        ExecBufferFlagCase(I915_EXEC_DEFAULT);
        ExecBufferFlagCase(I915_EXEC_RENDER);
        ExecBufferFlagCase(I915_EXEC_BSD);
        ExecBufferFlagCase(I915_EXEC_BLT);
        ExecBufferFlagCase(I915_EXEC_VEBOX);
      }

      DFLAG(I915_EXEC_CONSTANTS_MASK);
      DFLAG(I915_EXEC_CONSTANTS_REL_GENERAL);
      DFLAG(I915_EXEC_CONSTANTS_ABSOLUTE);
      DFLAG(I915_EXEC_CONSTANTS_REL_SURFACE);
      DFLAG(I915_EXEC_GEN7_SOL_RESET);
      DFLAG(I915_EXEC_SECURE);
      DFLAG(I915_EXEC_IS_PINNED);
      DFLAG(I915_EXEC_NO_RELOC);
      DFLAG(I915_EXEC_HANDLE_LUT);
      DFLAG(I915_EXEC_BSD_SHIFT);
      DFLAG(I915_EXEC_BSD_MASK);
      DFLAG(I915_EXEC_BSD_DEFAULT);
      DFLAG(I915_EXEC_BSD_RING1);
      DFLAG(I915_EXEC_BSD_RING2);
      DFLAG(I915_EXEC_RESOURCE_STREAMER);
      DFLAG(I915_EXEC_FENCE_IN);
      DFLAG(I915_EXEC_FENCE_OUT);
      DFLAG(I915_EXEC_BATCH_FIRST);
      DFLAG(I915_EXEC_FENCE_ARRAY);
      DFLAG(I915_EXEC_CONTEXT_ID_MASK);
    });

    DRF(batch_start_offset);
    DRF(batch_len);
    DRF(DR1);
    DRF(DR4);
    DRF(num_cliprects);
    DRF(cliprects_ptr);
    DRF(rsvd1);
    DRF(rsvd2);
  });

  if (DRM_COMMAND_BASE <= _IOC_TYPE(request) && _IOC_TYPE(request) <= DRM_COMMAND_END && !handle__skipped) {
    fprintf(stderr, "\033[94m" PREFIX "\033[33mioctl(%d, %lx) => ???\n", fd, request);
  }

  return ORIGINAL_HANDLER();
}
