/**
 * @file uds_version.h
 * @brief UDSLib Version Information
 */

#ifndef UDS_VERSION_H
#define UDS_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

/** Major version number (incompatible API changes) */
#define UDS_VERSION_MAJOR 1

/** Minor version number (backward-compatible functionality) */
#define UDS_VERSION_MINOR 8

/** Patch version number (backward-compatible bug fixes) */
#define UDS_VERSION_PATCH 0

/** Full version string */
#define UDS_VERSION_STR "1.8.0"

/** Version as a single integer for comparison (MMmmpp format) */
#define UDS_VERSION_INT ((UDS_VERSION_MAJOR * 10000) + (UDS_VERSION_MINOR * 100) + UDS_VERSION_PATCH)

#ifdef __cplusplus
}
#endif

#endif /* UDS_VERSION_H */
