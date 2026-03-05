find_path(LIBXCRYPT_INCLUDE_DIR crypt.h)
find_library(LIBXCRYPT_LIBRARY NAMES crypt)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libxcrypt
	DEFAULT_MSG
	LIBXCRYPT_LIBRARY
	LIBXCRYPT_INCLUDE_DIR)

mark_as_advanced(LIBXCRYPT_INCLUDE_DIR LIBXCRYPT_LIBRARY)
include_directories(${LIBXCRYPT_INCLUDE_DIR})
