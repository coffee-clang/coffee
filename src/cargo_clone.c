#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

void print_usage() {
    printf("Usage: cargo_clone <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  build, run, test, check, doc, bench, new, init, publish, install, update, search\n");
    printf("\n");
    printf("Global Options:\n");
    printf("  --help, -h                Show this help information\n");
    printf("  --version, -V             Show version information\n");
    printf("\n");
    printf("Common Command Options:\n");
    printf("  --release, -r             Build artifacts in release mode\n");
    printf("  --verbose, -v             Use verbose output\n");
    printf("  --quiet, -q               No output printed to stdout\n");
    printf("  --jobs, -j <N>            Number of parallel jobs, i.e. how many packages to build simultaneously\n");
    printf("  --target, -t <TARGET>     Target triple which compiles will be built for\n");
    printf("  --manifest-path, -m <PATH> Path to the manifest to transform\n");
    printf("  --features, -F <FEATURES>  Space-separated list of features to activate\n");
    printf("  --no-default-features     Do not activate the `default` feature\n");
    printf("  --all-features            Activate all available features\n");
    printf("  --color <WHEN>            Coloring: auto, always, or never\n");
    printf("  --frozen                  Require Cargo.lock and cache are up to date\n");
    printf("  --locked                  Require Cargo.lock is up to date\n");
    printf("  --offline                 Run without accessing network resources\n");
    printf("  --example, -e <NAME>      Run the specified example\n");
    printf("  --name, -n <NAME>         Set the project name (used by new/init commands)\n");
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    // Global options can be passed as the command if needed.
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0) {
        printf("cargo_clone version 1.0\n");
        return 0;
    }
    
    // Validate cargo commands
    char *command = argv[1];
    const char *allowed_commands[] = {"build", "run", "test", "check", "doc", "bench", "new", "init", "publish", "install", "update", "search"};
    int allowed = 0;
    int allowed_count = sizeof(allowed_commands) / sizeof(allowed_commands[0]);
    for (int i = 0; i < allowed_count; i++) {
        if (strcmp(command, allowed_commands[i]) == 0) {
            allowed = 1;
            break;
        }
    }
    if (!allowed) {
        printf("Unknown command: %s\n", command);
        print_usage();
        return 1;
    }
    
    // Reset getopt's global index to start parsing after the command name.
    optind = 2;
    int option_index = 0;
    int c;
    
    // Define long options replicating cargo options
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {"release", no_argument, 0, 'r'},
        {"verbose", no_argument, 0, 'v'},
        {"quiet", no_argument, 0, 'q'},
        {"jobs", required_argument, 0, 'j'},
        {"target", required_argument, 0, 't'},
        {"manifest-path", required_argument, 0, 'm'},
        {"features", required_argument, 0, 'F'},
        {"no-default-features", no_argument, 0, 1},
        {"all-features", no_argument, 0, 2},
        {"color", required_argument, 0, 3},
        {"frozen", no_argument, 0, 4},
        {"locked", no_argument, 0, 5},
        {"offline", no_argument, 0, 6},
        {"example", required_argument, 0, 'e'},
        {"name", required_argument, 0, 'n'},
        {0, 0, 0, 0}
    };
    
    // Short options string (only including options with a defined short option)
    const char *short_options = "hVr:vqj:t:m:F:e:n:";
    
    printf("Command: %s\n", command);
    
    // Parse command-specific options
    while ((c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
        switch(c) {
            case 'h':
                print_usage();
                return 0;
            case 'V':
                printf("cargo_clone version 1.0\n");
                return 0;
            case 'r':
                printf("Option --release provided\n");
                break;
            case 'v':
                printf("Option --verbose provided\n");
                break;
            case 'q':
                printf("Option --quiet provided\n");
                break;
            case 'j':
                printf("Option --jobs provided with value %s\n", optarg);
                break;
            case 't':
                printf("Option --target provided with value %s\n", optarg);
                break;
            case 'm':
                printf("Option --manifest-path provided with value %s\n", optarg);
                break;
            case 'F':
                printf("Option --features provided with value %s\n", optarg);
                break;
            case 1:
                printf("Option --no-default-features provided\n");
                break;
            case 2:
                printf("Option --all-features provided\n");
                break;
            case 3:
                printf("Option --color provided with value %s\n", optarg);
                break;
            case 4:
                printf("Option --frozen provided\n");
                break;
            case 5:
                printf("Option --locked provided\n");
                break;
            case 6:
                printf("Option --offline provided\n");
                break;
            case 'e':
                printf("Option --example provided with value %s\n", optarg);
                break;
            case 'n':
                printf("Option --name provided with value %s\n", optarg);
                break;
            case '?':
                // Error message already printed by getopt_long.
                break;
            default:
                printf("Unknown option encountered\n");
                break;
        }
    }
    
    // Print any remaining non-option positional arguments
    if (optind < argc) {
        printf("Positional arguments:\n");
        while(optind < argc) {
            printf("  %s\n", argv[optind++]);
        }
    }
    
    // Simulate execution of the cargo command
    printf("Executing cargo %s command...\n", command);
    
    return 0;
}
