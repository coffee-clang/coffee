/*
  Copyright (C) 2025 by the coffee developers
*/

#include "coffee.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t handle_add(options *) {
    printf("Handling command add\n");
    return 0;
}

static int64_t handle_build(options *) {
    printf("Handling command build\n");
    return 0;
}

static int64_t handle_bench(options *) {
    printf("Handling command bench\n");
    return 0;
}

static int64_t handle_check(options *) {
    printf("Handling command check\n");
    return 0;
}

static int64_t handle_clean(options *) {
    printf("Handling command clean\n");
    return 0;
}

static int64_t handle_compile(options *) {
    printf("Handling command compile\n");
    return 0;
}

static int64_t handle_config(options *) {
    printf("Handling command config\n");
    return 0;
}

static int64_t handle_doc(options *) {
    printf("Handling command doc\n");
    return 0;
}

static int64_t handle_lint(options *) {
    printf("Handling command lint\n");
    return 0;
}

static int64_t handle_fetch(options *) {
    printf("Handling command fetch\n");
    return 0;
}

static int64_t handle_fix(options *) {
    printf("Handling command fix\n");
    return 0;
}

static int64_t handle_fmt(options *) {
    printf("Handling command fmt\n");
    return 0;
}

static int64_t handle_generate_lockfile(options *) {
    printf("Handling command generate-lockfile\n");
    return 0;
}

static int64_t handle_help(options *) {
    printf("Handling command help\n");
    return 0;
}

static int64_t handle_info(options *) {
    printf("Handling command info\n");
    return 0;
}

static int64_t handle_init(options *) {
    printf("Handling command init\n");
    return 0;
}

static int64_t handle_install(options *) {
    printf("Handling command install\n");
    return 0;
}

static int64_t handle_install_update(options *) {
    printf("Handling command install-update\n");
    return 0;
}

static int64_t handle_install_update_config(options *) {
    printf("Handling command install-update-config\n");
    return 0;
}

static int64_t handle_locate_project(options *) {
    printf("Handling command locate-project\n");
    return 0;
}

static int64_t handle_login(options *) {
    printf("Handling command login\n");
    return 0;
}

static int64_t handle_logout(options *) {
    printf("Handling command logout\n");
    return 0;
}

static int64_t handle_machete(options *) {
    printf("Handling command machete\n");
    return 0;
}

static int64_t handle_metadata(options *) {
    printf("Handling command metadata\n");
    return 0;
}

static int64_t handle_miri(options *) {
    printf("Handling command miri\n");
    return 0;
}

static int64_t handle_new(options *) {
    printf("Handling command new\n");
    return 0;
}

static int64_t handle_owner(options *) {
    printf("Handling command owner\n");
    return 0;
}

static int64_t handle_package(options *) {
    printf("Handling command package\n");
    return 0;
}

static int64_t handle_pkgid(options *) {
    printf("Handling command pkgid\n");
    return 0;
}

static int64_t handle_publish(options *) {
    printf("Handling command publish\n");
    return 0;
}

static int64_t handle_remove(options *) {
    printf("Handling command remove\n");
    return 0;
}

static int64_t handle_report(options *) {
    printf("Handling command report\n");
    return 0;
}

static int64_t handle_rm(options *) {
    printf("Handling command rm\n");
    return 0;
}

static int64_t handle_run(options *) {
    printf("Handling command run\n");
    return 0;
}

static int64_t handle_search(options *) {
    printf("Handling command search\n");
    return 0;
}

static int64_t handle_test(options *) {
    printf("Handling command test\n");
    return 0;
}

static int64_t handle_tree(options *) {
    printf("Handling command tree\n");
    return 0;
}

static int64_t handle_uninstall(options *) {
    printf("Handling command uninstall\n");
    return 0;
}

static int64_t handle_update(options *) {
    printf("Handling command update\n");
    return 0;
}

static int64_t handle_vendor(options *) {
    printf("Handling command vendor\n");
    return 0;
}

static int64_t handle_version(options *) {
    printf("Handling command version\n");
    return 0;
}

static int64_t handle_yank(options *) {
    printf("Handling command yank\n");
    return 0;
}

command_s commands[] = {
    {
        .name = s8("add"),
        .description = s8("Add dependencies to a manifest file"),
        .action = &handle_add
    },
    {
        .name = s8("bench"),
        .description = s8("Benchmark"),
        .action = &handle_bench
    },
    {
        .name = s8("build"),
        .description = s8("Compile a local package and all of its dependencies"),
        .action = &handle_build
    },
    {
        .name = s8("b"),
        .description = s8("alias: build"),
        .action = &handle_build
    },
    {
        .name = s8("c"),
        .description = s8("alias: check"),
        .action = &handle_check
    },
    {
        .name = s8("check"),
        .description = s8("Check a local package and all of its dependencies"),
        .action = &handle_check
    },
    {
        .name = s8("clean"),
        .description = s8("Remove generated artifacts"),
        .action = &handle_clean
    },
    {
        .name = s8("compile"),
        .description = s8("Compile"),
        .action = &handle_compile
    },
    {
        .name = s8("config"),
        .description = s8("Inspect configuration values"),
        .action = &handle_config
    },
    {
        .name = s8("d"),
        .description = s8("alias: doc"),
        .action = &handle_doc
    },
    {
        .name = s8("doc"),
        .description = s8("Build a package's documentation"),
        .action = &handle_doc
    },
    {
        .name = s8("fetch"),
        .description = s8("Fetch dependencies of a package from the network"),
        .action = &handle_fetch
    },
    {
        .name = s8("fix"),
        .description = s8("Automatically fix lint warnings reported by the compiler"),
        .action = &handle_fix
    },
    {
        .name = s8("fmt"),
        .description = s8("Formats all bin and lib files of the current crate using rustfmt."),
        .action = &handle_fmt
    },
    {
        .name = s8("generate-lockfile"),
        .description = s8("Generate the lockfile for a package"),
        .action = &handle_generate_lockfile
    },
    {
        .name = s8("help"),
        .description = s8("Displays help for a cargo subcommand"),
        .action = &handle_help
    },
    {
        .name = s8("info"),
        .description = s8("Display information about a package"),
        .action = &handle_info
    },
    {
        .name = s8("init"),
        .description = s8("Create a new cargo package in an existing"),
        .action = &handle_init
    },
    {
        .name = s8("lint"),
        .description = s8("Lint the package"),
        .action = &handle_lint
    },
    {
        .name = s8("install"),
        .description = s8("Install a Rust binary"),
        .action = &handle_install
    },
    {
        .name = s8("install-update"),
        .description = s8("update"),
        .action = &handle_install_update
    },
    {
        .name = s8("install-update-config"),
        .description = s8("update-config"),
        .action = &handle_install_update_config
    },
    {
        .name = s8("locate-project"),
        .description = s8("Print a JSON representation of a Cargo.toml"),
        .action = &handle_locate_project
    },
    {
        .name = s8("login"),
        .description = s8("Log in to a registry."),
        .action = &handle_login
    },
    {
        .name = s8("logout"),
        .description = s8("Remove an API token from the registry locally"),
        .action = &handle_logout
    },
    {
        .name = s8("machete"),
        .description = s8(""),
        .action = &handle_machete
    },
    {
        .name = s8("metadata"),
        .description = s8("Output the resolved dependencies of a"),
        .action = &handle_metadata
    },
    {
        .name = s8("package"),
        .description = s8("the concrete used versions including overrides, in machine-readable format"),
        .action = &handle_package
    },
    {
        .name = s8("miri"),
        .description = s8(""),
        .action = &handle_miri
    },
    {
        .name = s8("new"),
        .description = s8("Create a new package at <path>"),
        .action = &handle_new
    },
    {
        .name = s8("owner"),
        .description = s8("Manage the owners of a crate on the registry"),
        .action = &handle_owner
    },
    {
        .name = s8("package"),
        .description = s8("Assemble the local package into a"),
        .action = &handle_package
    },
    {
        .name = s8("pkgid"),
        .description = s8("Print a fully qualified package specification"),
        .action = &handle_pkgid
    },
    {
        .name = s8("publish"),
        .description = s8("Upload a package to the registry"),
        .action = &handle_publish
    },
    {
        .name = s8("r"),
        .description = s8("alias: run"),
        .action = &handle_run
    },
    {
        .name = s8("remove"),
        .description = s8("Remove dependencies from a manifest file"),
        .action = &handle_remove
    },
    {
        .name = s8("report"),
        .description = s8("Generate and display various kinds of reports"),
        .action = &handle_report
    },
    {
        .name = s8("rm"),
        .description = s8("alias: remove"),
        .action = &handle_rm
    },
    {
        .name = s8("run"),
        .description = s8("Run a binary or example of the local package"),
        .action = &handle_run
    },
    {
        .name = s8("compile"),
        .description = s8("Compile a package, and pass extra options to"),
        .action = &handle_compile
    },
    {
        .name = s8("search"),
        .description = s8("Search packages in the registry. Default"),
        .action = &handle_search
    },
    {
        .name = s8("t"),
        .description = s8("alias: test"),
        .action = &handle_test
    },
    {
        .name = s8("test"),
        .description = s8("Execute all unit and integration tests and"),
        .action = &handle_test
    },
    {
        .name = s8("tree"),
        .description = s8("Display a tree visualization of a dependency"),
        .action = &handle_tree
    },
    {
        .name = s8("uninstall"),
        .description = s8("Remove a Rust binary"),
        .action = &handle_uninstall
    },
    {
        .name = s8("update"),
        .description = s8("Update dependencies as recorded in the local"),
        .action = &handle_update
    },
    {
        .name = s8("vendor"),
        .description = s8("Vendor all dependencies for a project locally"),
        .action = &handle_vendor
    },
    {
        .name = s8("version"),
        .description = s8("Show version information"),
        .action = &handle_version
    },
    {
        .name = s8("yank"),
        .description = s8("Remove a pushed crate from the index"),
        .action = &handle_yank
    },
};
int main(int argc, char **argv) {
    static struct gengetopt_args_info args_info;
    cmdline_parser(argc, argv, &args_info);

    printf("Arguments number: %d\n", args_info.inputs_num);
    // Find first non-toolchain argument (commands start with letter)
    int command_idx = -1;
    for (int i = 0; i < args_info.inputs_num; i++) {
        if (args_info.inputs[i][0] != '+') {
            command_idx = i;
            break;
        }
    }

    if (command_idx == -1) {
        // No command found - show available commands
        printf("Installed Commands:\n");
        for (int64_t i = 0; i < lengthof(commands); i++) {
            printf("%s\t%s\n", commands[i].name, commands[i].description);
        }
        exit(EXIT_SUCCESS);
    }

    s8 cmd = s8(args_info.inputs[command_idx]);
    printf("Command is: %s\n", cmd);

    // Find matching command
    for (int64_t i = 0; i < lengthof(commands); i++) {
        if (s8compare(s8(cmd), commands[i].name) == 0) {
            commands[i].action(NULL);
            exit(EXIT_SUCCESS);
        }
    }

    printf("Command '%s' is not recognized.\n", cmd);
    return EXIT_FAILURE;
}
// AI! explain the error coffee.c:496:9: error: cannot convert to a pointer type

