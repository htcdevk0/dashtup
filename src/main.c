#include "config.h"
#include "fs.h"
#include "tar_reader.h"
#include "ui.h"

#include <errno.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

extern const unsigned char _binary_build_payload_tar_start[];
extern const unsigned char _binary_build_payload_tar_end[];

static const unsigned char *payload_data(void) {
    return _binary_build_payload_tar_start;
}

static size_t payload_size(void) {
    return (size_t)(_binary_build_payload_tar_end - _binary_build_payload_tar_start);
}

enum {
    INSTALL_NONE = 0,
    INSTALL_BIN  = 1 << 0,
    INSTALL_STD  = 1 << 1,
    INSTALL_DOC  = 1 << 2,
    INSTALL_ALL  = INSTALL_BIN | INSTALL_STD | INSTALL_DOC
};

typedef struct {
    char *prefix;
    char *home;
    char *bin_dir;
    char *bin_path;
    char *backup_path;
    char *dash_home;
    char *doc_dir;
    char *doc_txt_path;
    char *doc_md_path;
    char *state_path;
    bool assume_yes;
    uid_t home_uid;
    gid_t home_gid;
} app_paths_t;

static void free_paths(app_paths_t *paths) {
    free(paths->prefix);
    free(paths->home);
    free(paths->bin_dir);
    free(paths->bin_path);
    free(paths->backup_path);
    free(paths->dash_home);
    free(paths->doc_dir);
    free(paths->doc_txt_path);
    free(paths->doc_md_path);
    free(paths->state_path);
}

static const char *yes_no(bool value) {
    return value ? "yes" : "no";
}

static const char *install_scope_label(int scope) {
    if (scope == INSTALL_BIN) {
        return "binary only";
    }
    if (scope == INSTALL_STD) {
        return "stdlib only";
    }
    if (scope == INSTALL_DOC) {
        return "documentation only";
    }
    if (scope == (INSTALL_STD | INSTALL_DOC)) {
        return "stdlib + documentation";
    }
    return "binary + stdlib + documentation";
}

static void die_errno(const char *message) {
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s: %s", message, strerror(errno));
    ui_error(buffer);
    exit(1);
}

static char *dup_env_or_default(const char *name, const char *fallback) {
    const char *value = getenv(name);
    if (value && value[0] != '\0') {
        return strdup(value);
    }
    return strdup(fallback);
}

static bool resolve_home_identity(char **home_out, uid_t *uid_out, gid_t *gid_out) {
    const char *env_home = getenv(DASHTUP_ENV_HOME);
    const char *sudo_user = getenv("SUDO_USER");
    struct passwd *pw = NULL;

    if (env_home && env_home[0] != '\0') {
        *home_out = strdup(env_home);
        if (!*home_out) {
            return false;
        }
        *uid_out = getuid();
        *gid_out = getgid();
        return true;
    }

    if (sudo_user && sudo_user[0] != '\0') {
        pw = getpwnam(sudo_user);
        if (pw && pw->pw_dir && pw->pw_dir[0] != '\0') {
            *home_out = strdup(pw->pw_dir);
            if (!*home_out) {
                return false;
            }
            *uid_out = pw->pw_uid;
            *gid_out = pw->pw_gid;
            return true;
        }
    }

    pw = getpwuid(getuid());
    if (pw && pw->pw_dir && pw->pw_dir[0] != '\0') {
        *home_out = strdup(pw->pw_dir);
        if (!*home_out) {
            return false;
        }
        *uid_out = pw->pw_uid;
        *gid_out = pw->pw_gid;
        return true;
    }

    env_home = getenv("HOME");
    if (env_home && env_home[0] != '\0') {
        *home_out = strdup(env_home);
        if (!*home_out) {
            return false;
        }
        *uid_out = getuid();
        *gid_out = getgid();
        return true;
    }

    return false;
}

static int build_paths(app_paths_t *paths) {
    memset(paths, 0, sizeof(*paths));

    paths->prefix = dup_env_or_default(DASHTUP_ENV_PREFIX, "/");
    if (!paths->prefix || !resolve_home_identity(&paths->home, &paths->home_uid, &paths->home_gid)) {
        return -1;
    }

    paths->bin_dir = fs_join2(paths->prefix, "bin");
    paths->bin_path = fs_join2(paths->bin_dir, "dash");
    paths->backup_path = fs_join2(paths->bin_dir, DASHTUP_BACKUP_NAME);
    paths->dash_home = fs_join2(paths->home, ".dash");
    paths->doc_dir = fs_join2(paths->dash_home, "doc");
    paths->doc_txt_path = fs_join2(paths->doc_dir, "DASH_en.txt");
    paths->doc_md_path = fs_join2(paths->doc_dir, "DASH_en.md");
    paths->state_path = fs_join2(paths->dash_home, DASHTUP_STATE_FILE);

    if (!paths->bin_dir || !paths->bin_path || !paths->backup_path ||
        !paths->dash_home || !paths->doc_dir || !paths->doc_txt_path || !paths->doc_md_path || !paths->state_path) {
        return -1;
    }

    paths->assume_yes = false;
    {
        const char *assume = getenv(DASHTUP_ENV_ASSUME_YES);
        if (assume && assume[0] == '1') {
            paths->assume_yes = true;
        }
    }

    return 0;
}

static bool ask(const app_paths_t *paths, const char *question, bool default_yes) {
    if (paths->assume_yes) {
        return true;
    }
    return ui_confirm(question, default_yes);
}

static bool is_doc_resource(const char *resource_name) {
    return strncmp(resource_name, ".dash/doc/", 10) == 0;
}

static bool resource_matches_scope(const char *resource_name, int scope) {
    if ((scope & INSTALL_BIN) && strncmp(resource_name, "bin/", 4) == 0) {
        return true;
    }
    if ((scope & INSTALL_STD) && strncmp(resource_name, ".dash/", 6) == 0 && !is_doc_resource(resource_name)) {
        return true;
    }
    if ((scope & INSTALL_DOC) && is_doc_resource(resource_name)) {
        return true;
    }
    return false;
}

static char *map_resource_to_target(const app_paths_t *paths, const char *resource_name) {
    if (strncmp(resource_name, "bin/", 4) == 0) {
        return fs_join2(paths->prefix, resource_name);
    }
    if (strncmp(resource_name, ".dash/", 6) == 0) {
        return fs_join2(paths->home, resource_name);
    }
    return NULL;
}

static int write_state_file(const app_paths_t *paths, int scope) {
    char buffer[1400];
    time_t now = time(NULL);
    int len = snprintf(
        buffer,
        sizeof(buffer),
        "dashtup_version=%s\n"
        "dash_version=%s\n"
        "doc_version=%s\n"
        "prefix=%s\n"
        "home=%s\n"
        "bin_path=%s\n"
        "doc_dir=%s\n"
        "backup_path=%s\n"
        "scope=%s\n"
        "installed_at=%lld\n",
        DASHTUP_VERSION,
        DASH_PACKAGE_VERSION,
        DASH_DOC_VERSION,
        paths->prefix,
        paths->home,
        paths->bin_path,
        paths->doc_dir,
        paths->backup_path,
        install_scope_label(scope),
        (long long)now
    );
    if (len < 0) {
        return -1;
    }
    return fs_write_file(paths->state_path, (const unsigned char *)buffer, (size_t)len, 0644);
}

static int install_payload(const app_paths_t *paths, int scope) {
    tar_reader_t reader;
    tar_entry_t entry;

    tar_reader_init(&reader, payload_data(), payload_size());
    while (tar_reader_next(&reader, &entry)) {
        char *target;
        if (!resource_matches_scope(entry.name, scope)) {
            continue;
        }

        target = map_resource_to_target(paths, entry.name);
        if (!target) {
            continue;
        }

        if (entry.typeflag == '5') {
            if (fs_mkdir_p(target, entry.mode ? entry.mode : 0755) != 0) {
                free(target);
                return -1;
            }
        } else if (entry.typeflag == '0') {
            mode_t mode = entry.mode ? entry.mode : 0644;
            if (fs_write_file(target, entry.data, entry.size, mode) != 0) {
                free(target);
                return -1;
            }
        }

        free(target);
    }

    return 0;
}

static bool file_matches_packaged_dash(const char *path) {
    unsigned char *disk = NULL;
    size_t disk_size = 0;
    tar_reader_t reader;
    tar_entry_t entry;
    bool matches = false;

    if (fs_read_file(path, &disk, &disk_size) != 0) {
        return false;
    }

    tar_reader_init(&reader, payload_data(), payload_size());
    while (tar_reader_next(&reader, &entry)) {
        if (strcmp(entry.name, "bin/dash") == 0) {
            matches = (disk_size == entry.size && memcmp(disk, entry.data, entry.size) == 0);
            break;
        }
    }

    free(disk);
    return matches;
}

static int apply_home_ownership(const app_paths_t *paths) {
    if (geteuid() != 0) {
        return 0;
    }
    return fs_chown_tree(paths->dash_home, paths->home_uid, paths->home_gid);
}

static bool docs_installed(const app_paths_t *paths) {
    return fs_exists(paths->doc_txt_path) && fs_exists(paths->doc_md_path);
}

static bool stdlib_installed(const app_paths_t *paths) {
    char *imports_dir = fs_join2(paths->dash_home, "imports");
    char *static_dir = fs_join2(paths->dash_home, "static");
    char *lib_dir = fs_join2(paths->dash_home, "lib");
    char *license_path = fs_join2(paths->dash_home, "LICENSE");
    bool installed = false;

    if (!imports_dir || !static_dir || !lib_dir || !license_path) {
        free(imports_dir);
        free(static_dir);
        free(lib_dir);
        free(license_path);
        return false;
    }

    installed = fs_is_dir(imports_dir) || fs_is_dir(static_dir) || fs_is_dir(lib_dir) || fs_exists(license_path);

    free(imports_dir);
    free(static_dir);
    free(lib_dir);
    free(license_path);
    return installed;
}

static void print_install_summary(const app_paths_t *paths, int scope) {
    char buffer[1024];
    ui_title("Install summary");
    snprintf(buffer, sizeof(buffer), "Packaged Dash version: %s", DASH_PACKAGE_VERSION);
    ui_info(buffer);
    snprintf(buffer, sizeof(buffer), "Documentation version: %s", DASH_DOC_VERSION);
    ui_info(buffer);
    snprintf(buffer, sizeof(buffer), "Installer version: %s", DASHTUP_VERSION);
    ui_info(buffer);
    snprintf(buffer, sizeof(buffer), "Scope: %s", install_scope_label(scope));
    ui_info(buffer);
    if (scope & INSTALL_BIN) {
        snprintf(buffer, sizeof(buffer), "Binary target: %s", paths->bin_path);
        ui_info(buffer);
        snprintf(buffer, sizeof(buffer), "Needs permission for %s: %s", paths->bin_dir, yes_no(access(paths->bin_dir, W_OK) == 0));
        ui_info(buffer);
    }
    if (scope & (INSTALL_STD | INSTALL_DOC)) {
        snprintf(buffer, sizeof(buffer), "Home target: %s", paths->dash_home);
        ui_info(buffer);
        snprintf(buffer, sizeof(buffer), "Home owner: uid=%lu gid=%lu", (unsigned long)paths->home_uid, (unsigned long)paths->home_gid);
        ui_info(buffer);
    }
    if (scope & INSTALL_DOC) {
        snprintf(buffer, sizeof(buffer), "Documentation target: %s", paths->doc_dir);
        ui_info(buffer);
    }
}

static void maybe_prepare_bin_target(const app_paths_t *paths) {
    if (fs_mkdir_p(paths->bin_dir, 0755) != 0) {
        die_errno("Unable to prepare the binary directory. Try running with elevated privileges");
    }
}

static void maybe_handle_existing_bin(const app_paths_t *paths) {
    if (!fs_exists(paths->bin_path)) {
        return;
    }

    if (file_matches_packaged_dash(paths->bin_path)) {
        ui_info("The packaged Dash binary is already installed at the target path.");
        return;
    }

    ui_warn("A different file already exists at the target binary path.");
    if (!ask(paths, "Back it up and replace it with the packaged Dash binary?", true)) {
        ui_warn("Installation cancelled.");
        exit(0);
    }

    if (fs_exists(paths->backup_path) && fs_remove_file_if_exists(paths->backup_path) != 0) {
        die_errno("Unable to remove the previous backup file");
    }
    if (fs_move_replace(paths->bin_path, paths->backup_path) != 0) {
        die_errno("Unable to create a backup for the existing binary");
    }
    ui_info("Existing binary moved to backup.");
}

static void remove_target_tree_or_file(const char *path, const char *description) {
    if (fs_is_dir(path)) {
        if (fs_remove_tree(path) != 0) {
            die_errno(description);
        }
        return;
    }
    if (fs_exists(path)) {
        if (fs_remove_file_if_exists(path) != 0) {
            die_errno(description);
        }
    }
}

static void maybe_handle_existing_std(const app_paths_t *paths) {
    bool has_existing;
    char *imports_dir = fs_join2(paths->dash_home, "imports");
    char *static_dir = fs_join2(paths->dash_home, "static");
    char *lib_dir = fs_join2(paths->dash_home, "lib");
    char *license_path = fs_join2(paths->dash_home, "LICENSE");

    if (!imports_dir || !static_dir || !lib_dir || !license_path) {
        free(imports_dir);
        free(static_dir);
        free(lib_dir);
        free(license_path);
        errno = ENOMEM;
        die_errno("Unable to prepare stdlib replacement paths");
    }

    has_existing = fs_is_dir(imports_dir) || fs_is_dir(static_dir) || fs_is_dir(lib_dir) || fs_exists(license_path);
    if (!has_existing) {
        free(imports_dir);
        free(static_dir);
        free(lib_dir);
        free(license_path);
        return;
    }

    ui_warn("An existing Dash stdlib installation was found in the target home directory.");
    if (!ask(paths, "Replace the packaged stdlib files in ~/.dash?", true)) {
        free(imports_dir);
        free(static_dir);
        free(lib_dir);
        free(license_path);
        ui_warn("Installation cancelled.");
        exit(0);
    }

    remove_target_tree_or_file(imports_dir, "Unable to remove the existing imports directory");
    remove_target_tree_or_file(static_dir, "Unable to remove the existing static directory");
    remove_target_tree_or_file(lib_dir, "Unable to remove the existing shared library directory");
    remove_target_tree_or_file(license_path, "Unable to remove the existing Dash license file");
    if (fs_remove_file_if_exists(paths->state_path) != 0) {
        die_errno("Unable to remove the previous installer state file");
    }

    free(imports_dir);
    free(static_dir);
    free(lib_dir);
    free(license_path);
    ui_info("Previous stdlib files removed.");
}

static void maybe_handle_existing_doc(const app_paths_t *paths) {
    if (!fs_is_dir(paths->doc_dir) && !docs_installed(paths)) {
        return;
    }

    ui_warn("An existing Dash documentation directory was found in the target home directory.");
    if (!ask(paths, "Replace the packaged documentation files in ~/.dash/doc?", true)) {
        ui_warn("Installation cancelled.");
        exit(0);
    }

    remove_target_tree_or_file(paths->doc_dir, "Unable to remove the existing documentation directory");
    ui_info("Previous documentation files removed.");
}

static void command_install(const app_paths_t *paths, int scope) {
    int step = 1;
    int total = (scope & (INSTALL_STD | INSTALL_DOC)) ? 5 : 4;

    ui_banner();
    ui_blank();
    print_install_summary(paths, scope);
    ui_blank();

    if (!ask(paths, "Proceed with installation?", true)) {
        ui_warn("Installation cancelled.");
        return;
    }

    ui_blank();
    ui_step(step++, total, "Preparing target directories");
    if (scope & INSTALL_BIN) {
        maybe_prepare_bin_target(paths);
    }
    if (scope & (INSTALL_STD | INSTALL_DOC)) {
        if (fs_mkdir_p(paths->home, 0755) != 0) {
            die_errno("Unable to prepare the target home directory");
        }
        if (fs_mkdir_p(paths->dash_home, 0755) != 0) {
            die_errno("Unable to prepare the target .dash directory");
        }
    }

    ui_step(step++, total, "Checking existing install targets");
    if (scope & INSTALL_BIN) {
        maybe_handle_existing_bin(paths);
    }
    if (scope & INSTALL_STD) {
        maybe_handle_existing_std(paths);
    }
    if (scope & INSTALL_DOC) {
        maybe_handle_existing_doc(paths);
    }

    ui_step(step++, total, "Extracting embedded resources");
    if (install_payload(paths, scope) != 0) {
        die_errno("Unable to extract embedded resources");
    }

    ui_step(step++, total, "Applying target ownership");
    if ((scope & (INSTALL_STD | INSTALL_DOC)) && apply_home_ownership(paths) != 0) {
        die_errno("Unable to apply the correct ownership to the installed home resources");
    }

    if (scope & (INSTALL_STD | INSTALL_DOC)) {
        ui_step(step++, total, "Writing installer state");
        if (write_state_file(paths, scope) != 0) {
            die_errno("Unable to write the installer state file");
        }
        if (apply_home_ownership(paths) != 0) {
            die_errno("Unable to apply the correct ownership to the installer state file");
        }
    }

    ui_blank();
    ui_success("Dash was installed successfully.");
    if (scope & INSTALL_BIN) {
        printf("  Binary : %s\n", paths->bin_path);
        if (fs_exists(paths->backup_path)) {
            printf("  Backup : %s\n", paths->backup_path);
        }
    }
    if (scope & INSTALL_STD) {
        printf("  Stdlib : %s\n", paths->dash_home);
    }
    if (scope & INSTALL_DOC) {
        printf("  Docs   : %s\n", paths->doc_dir);
        printf("  Files  : %s, %s\n", paths->doc_txt_path, paths->doc_md_path);
    }
}

static void print_uninstall_summary(const app_paths_t *paths) {
    char buffer[1024];
    ui_title("Uninstall summary");
    snprintf(buffer, sizeof(buffer), "Binary target: %s", paths->bin_path);
    ui_info(buffer);
    snprintf(buffer, sizeof(buffer), "Backup binary: %s", paths->backup_path);
    ui_info(buffer);
    snprintf(buffer, sizeof(buffer), "Home target: %s", paths->dash_home);
    ui_info(buffer);
    snprintf(buffer, sizeof(buffer), "Documentation target: %s", paths->doc_dir);
    ui_info(buffer);
}

static void command_uninstall(const app_paths_t *paths) {
    ui_banner();
    ui_blank();
    print_uninstall_summary(paths);
    ui_blank();

    if (!ask(paths, "Proceed with uninstallation?", true)) {
        ui_warn("Uninstallation cancelled.");
        return;
    }

    ui_blank();
    ui_step(1, 4, "Removing the ~/.dash directory");
    if (fs_is_dir(paths->dash_home)) {
        if (fs_remove_tree(paths->dash_home) != 0) {
            die_errno("Unable to remove the .dash directory");
        }
        ui_info("Removed the .dash directory.");
    } else {
        ui_info("No .dash directory was found.");
    }

    ui_step(2, 4, "Removing the installed Dash binary");
    if (fs_exists(paths->bin_path)) {
        if (fs_remove_file_if_exists(paths->bin_path) != 0) {
            die_errno("Unable to remove the installed binary");
        }
        ui_info("Removed the installed Dash binary.");
    } else {
        ui_info("No installed Dash binary was found at the target path.");
    }

    ui_step(3, 4, "Restoring the previous binary if a backup exists");
    if (fs_exists(paths->backup_path)) {
        if (fs_move_replace(paths->backup_path, paths->bin_path) != 0) {
            die_errno("Unable to restore the backup binary");
        }
        ui_info("Backup binary restored.");
    } else {
        ui_info("No backup binary was found.");
    }

    ui_step(4, 4, "Cleaning leftover installer state");
    if (fs_remove_file_if_exists(paths->state_path) == 0) {
        ui_info("Installer state cleaned.");
    }

    ui_blank();
    ui_success("Dash was removed successfully.");
}

static void command_version(const app_paths_t *paths) {
    const char *binary_status = "not installed";
    const char *std_status = stdlib_installed(paths) ? "installed" : "not installed";
    const char *doc_status = docs_installed(paths) ? "installed" : "not installed";

    ui_banner();
    ui_blank();
    ui_title("Version information");

    if (fs_exists(paths->bin_path)) {
        binary_status = file_matches_packaged_dash(paths->bin_path)
            ? "installed (packaged Dash)"
            : "occupied by another file";
    }

    printf("dashtup : %s\n", DASHTUP_VERSION);
    printf("dash    : %s\n", DASH_PACKAGE_VERSION);
    printf("docs    : %s (%s)\n", doc_status, DASH_DOC_VERSION);
    printf("binary  : %s\n", binary_status);
    printf("stdlib  : %s\n", std_status);
    printf("target  : %s\n", paths->bin_path);
    printf("home dir: %s\n", paths->dash_home);
    printf("doc dir : %s\n", paths->doc_dir);
}

static void command_license(void) {
    ui_banner();
    ui_blank();
    ui_title("License information");
    printf("Project : Dashtup™\n");
    printf("Type    : Dash Programming Language™ installer\n");
    printf("Creator : htcdevk0\n");
    printf("Version : %s\n", DASHTUP_VERSION);
    printf("License : GNU GPLv3\n");
    printf("Mark    : TM\n");
}

static void print_usage(FILE *stream, const char *argv0) {
    fprintf(stream,
            "Usage: %s <command> [target]\n"
            "\n"
            "Commands:\n"
            "  install           Install /bin/dash, ~/.dash stdlib and ~/.dash/doc\n"
            "  install bin       Install only /bin/dash\n"
            "  install std       Install only the Dash stdlib into ~/.dash\n"
            "  install doc       Install only the Dash docs into ~/.dash/doc\n"
            "  uninstall         Remove Dash and restore a backup binary if one exists\n"
            "  version           Show dashtup, Dash and docs versions\n"
            "  license           Show dashtup license information\n"
            "  help              Show this help message\n"
            "\n"
            "Examples:\n"
            "  %s install\n"
            "  %s install bin\n"
            "  %s install std\n"
            "  %s install doc\n"
            "  sudo %s install\n",
            argv0, argv0, argv0, argv0, argv0, argv0);
}

static int parse_install_scope(int argc, char **argv, int *scope_out) {
    if (argc == 2) {
        *scope_out = INSTALL_ALL;
        return 0;
    }
    if (argc != 3) {
        return -1;
    }
    if (strcmp(argv[2], "bin") == 0) {
        *scope_out = INSTALL_BIN;
        return 0;
    }
    if (strcmp(argv[2], "std") == 0) {
        *scope_out = INSTALL_STD;
        return 0;
    }
    if (strcmp(argv[2], "doc") == 0) {
        *scope_out = INSTALL_DOC;
        return 0;
    }
    if (strcmp(argv[2], "all") == 0) {
        *scope_out = INSTALL_ALL;
        return 0;
    }
    return -1;
}

int main(int argc, char **argv) {
    app_paths_t paths;

    ui_init();

    if (build_paths(&paths) != 0) {
        ui_error("Unable to resolve the installation paths.");
        return 1;
    }

    if (argc < 2 || argc > 3) {
        print_usage(stderr, argv[0]);
        free_paths(&paths);
        return 1;
    }

    if (strcmp(argv[1], "install") == 0) {
        int scope = INSTALL_ALL;
        if (parse_install_scope(argc, argv, &scope) != 0) {
            print_usage(stderr, argv[0]);
            free_paths(&paths);
            return 1;
        }
        command_install(&paths, scope);
    } else if (strcmp(argv[1], "uninstall") == 0) {
        if (argc != 2) {
            print_usage(stderr, argv[0]);
            free_paths(&paths);
            return 1;
        }
        command_uninstall(&paths);
    } else if (strcmp(argv[1], "version") == 0) {
        if (argc != 2) {
            print_usage(stderr, argv[0]);
            free_paths(&paths);
            return 1;
        }
        command_version(&paths);
    } else if (strcmp(argv[1], "license") == 0) {
        if (argc != 2) {
            print_usage(stderr, argv[0]);
            free_paths(&paths);
            return 1;
        }
        command_license();
    } else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(stdout, argv[0]);
    } else {
        print_usage(stderr, argv[0]);
        free_paths(&paths);
        return 1;
    }

    free_paths(&paths);
    return 0;
}
