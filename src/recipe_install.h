#ifndef RECIPE_INSTALL_H_
#define RECIPE_INSTALL_H_

#include "coffee.h"
#include "lockfile.h"

/*
 * Install registry packages through their recipe's install.sh.
 *
 * The recipe (library.toml and install.sh) is downloaded into
 * $COFFEE_HOME/recipes/<name>/ and the script is run with COFFEE_HOME set.
 * It fetches the upstream source (git or archive), builds it and installs
 * it into the prefix $COFFEE_HOME/<name>/<version>/, with headers in
 * include/ and libraries in lib/.  Coffee then copies the recipe's
 * library.toml into the prefix, so the dependency graph sees the package's
 * version and its own dependencies, and writes the stamp
 * <prefix>/.coffee-installed holding the sha256 of the install.sh that
 * produced the install.
 */

enum recipe_status {
	RECIPE_INSTALLED, /* installed, or already installed by the same install.sh */
	RECIPE_NO_SCRIPT, /* the recipe exists but ships no install.sh */
	RECIPE_NOT_FOUND, /* the recipe could not be downloaded */
	RECIPE_FAILED,    /* the install failed; the error has been reported */
};

struct recipe_info {
	sds prefix;   /* $COFFEE_HOME/<name>/<version> */
	sds version;  /* recipe version */
	sds checksum; /* sha256 of the install.sh */
};

/*
 * Install the registry package name.  The recipe's dependencies are
 * installed first, so its install.sh can build against them.  constraint
 * (nullable) must be satisfied by the recipe version.  When locked is
 * non-null, the recipe version and the install.sh checksum must match it.
 * On RECIPE_INSTALLED, *info is filled (free with recipe_info_free).
 */
enum recipe_status recipe_install(const char *name, const char *constraint, const lockfile_dep_t *locked, bool verbose,
                                  struct recipe_info *info);

/*
 * Read the version and checksum of a package installed by recipe_install()
 * in dir (a prefix, or a symlink to one) without touching the network.
 * Returns false when dir holds no completed recipe install.  On success,
 * *info is filled with the resolved prefix (free with recipe_info_free).
 */
bool recipe_read_installed(const char *dir, struct recipe_info *info);

void recipe_info_free(struct recipe_info *info);

#endif /* RECIPE_INSTALL_H_ */
