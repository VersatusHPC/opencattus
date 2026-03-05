/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_CONST_H_
#define OPENCATTUS_CONST_H_

// We strive for constexpr correctness, but we sometimes still need a macro
namespace opencattus {

#define PRODUCT_NAME "OpenCATTUS"
constexpr const char* productName = PRODUCT_NAME;

#define PRODUCT_VERSION "0.1 Alpha"
constexpr const char* productVersion = PRODUCT_VERSION;

#define PRODUCT_URL "https://github.com/versatushpc/opencattus"
constexpr const char* productUrl = PRODUCT_URL;

#define INSTALL_PATH "/opt/opencattus"
constexpr const char* installPath = INSTALL_PATH;

#if defined(_DUMMY_) || defined(__APPLE__)
#define CHROOT "chroot"
[[maybe_unused]] constexpr const char* chroot = CHROOT;
#else
#define CHROOT
#endif

} // namespace opencattus

#define OK 0

using opencattus::installPath;
using opencattus::productName;
using opencattus::productUrl;
using opencattus::productVersion;

#endif // OPENCATTUS_CONST_H_
