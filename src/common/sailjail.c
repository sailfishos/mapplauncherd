#include "sailjail.h"
#include "report.h"

#include <stdio.h>
#include <limits.h>

#include <dbus/dbus.h>

/* Standard desktop properties */
#define DESKTOP_SECTION "Desktop Entry"
#define DESKTOP_KEY_NAME "Name"
#define DESKTOP_KEY_TYPE "Type"
#define DESKTOP_KEY_ICON "Icon"
#define DESKTOP_KEY_EXEC "Exec"
#define DESKTOP_KEY_NO_DISPLAY "NoDisplay"

/* Maemo desktop properties */
#define MAEMO_SECTION "Desktop Entry"
#define MAEMO_KEY_SERVICE "X-Maemo-Service"
#define MAEMO_KEY_OBJECT "X-Maemo-Object-Path"
#define MAEMO_KEY_METHOD "X-Maemo-Method"

/* Sailjail desktop properties */
#define SAILJAIL_SECTION_PRIMARY "X-Sailjail"
#define SAILJAIL_SECTION_SECONDARY "Sailjail"
#define SAILJAIL_KEY_ORGANIZATION_NAME "OrganizationName"
#define SAILJAIL_KEY_APPLICATION_NAME "ApplicationName"
#define SAILJAIL_KEY_PERMISSIONS "Permissions"
#define SAILJAIL_KEY_MODE "Mode"
#define SAILJAIL_KEY_EXEC_DBUS "ExecDBus"
#define NEMO_KEY_APPLICATION_TYPE "X-Nemo-Application-Type"
#define NEMO_KEY_SINGLE_INSTANCE "X-Nemo-Single-Instance"
#define MAEMO_KEY_FIXED_ARGS "X-Maemo-Fixed-Args"
#define OSSO_KEY_SERVICE "X-Osso-Service"

/* Sailjaild D-Bus service */
#define PERMISSIONMGR_SERVICE "org.sailfishos.sailjaild1"
#define PERMISSIONMGR_INTERFACE "org.sailfishos.sailjaild1"
#define PERMISSIONMGR_OBJECT "/org/sailfishos/sailjaild1"
#define PERMISSIONMGR_METHOD_PROMPT "PromptLaunchPermissions"
#define PERMISSIONMGR_METHOD_QUERY "QueryLaunchPermissions"
#define PERMISSIONMGR_METHOD_GET_APPLICATIONS "GetApplications"
#define PERMISSIONMGR_METHOD_GET_APPINFO "GetAppInfo"
#define PERMISSIONMGR_METHOD_GET_LICENSE "GetLicenseAgreed"
#define PERMISSIONMGR_METHOD_SET_LICENSE "SetLicenseAgreed"
#define PERMISSIONMGR_METHOD_GET_LAUNCHABLE "GetLaunchAllowed"
#define PERMISSIONMGR_METHOD_SET_LAUNCHABLE "SetLaunchAllowed"
#define PERMISSIONMGR_METHOD_GET_GRANTED "GetGrantedPermissions"
#define PERMISSIONMGR_METHOD_SET_GRANTED "SetGrantedPermissions"
#define PERMISSIONMGR_SIGNAL_APP_ADDED "ApplicationAdded"
#define PERMISSIONMGR_SIGNAL_APP_CHANGED "ApplicationChanged"
#define PERMISSIONMGR_SIGNAL_APP_REMOVED "ApplicationRemoved"

/* Sailjaild errors */
#define CODE_INVALID_ARGS     "org.freedesktop.DBus.Error.InvalidArgs"
#define ERROR_INVALID_APPNAME "Invalid application name: "

/* Directories */
#define BINDIR "/usr/bin"

const gchar *
path_basename(const gchar *path)
{
    const gchar *base = NULL;
    if( path ) {
        gchar *end = strrchr(path, '/');
        base = end ? end + 1 : path;
    }
    return base;
}

static bool
path_dirname_eq(const char *path, const char *target)
{
    gchar *dir_path = g_path_get_dirname(path);
    bool result = !strcmp(dir_path, target);
    g_free(dir_path);
    return result;
}

static DBusConnection *
sailjail_connect_bus(void)
{
    DBusError err = DBUS_ERROR_INIT;
    DBusConnection *con = 0;

    if (!(con = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err))) {
        error("system bus connect failed: %s: %s",
              err.name, err.message);
    } else {
        debug("PRIVATE CONNECTION %p CONNECTED", con);
        dbus_connection_set_exit_on_disconnect(con, false);
    }

    dbus_error_free(&err);
    return con;
}

static void
sailjail_disconnect_bus(DBusConnection *con)
{
    if (con) {
        debug("PRIVATE CONNECTION %p DISCONNECTED", con);
        dbus_connection_close(con);
        dbus_connection_unref(con);
    }
}

static GVariant *
appinfo_variant(GHashTable *info, const char *key)
{
    return g_hash_table_lookup(info, key);
}

static const char *
appinfo_string(GHashTable *info, const char *key)
{
    const char *value = NULL;
    GVariant *variant = appinfo_variant(info, key);
    if (variant) {
        const GVariantType *type = g_variant_get_type(variant);
        if (g_variant_type_equal(type, G_VARIANT_TYPE_STRING))
            value = g_variant_get_string(variant, NULL);
    }
    return value;
}

static const char **
appinfo_strv(GHashTable *info, const char *key)
{
    const char **value = NULL;
    GVariant *variant = appinfo_variant(info, key);
    if (variant) {
        const GVariantType *type = g_variant_get_type(variant);
        if (g_variant_type_equal(type, G_VARIANT_TYPE("as")))
            value = g_variant_get_strv(variant, NULL);
    }
    return value;
}

static bool
iter_at(DBusMessageIter *iter, int type)
{
    int have = dbus_message_iter_get_arg_type(iter);
    if (have != type && type != 0)
        warning("Expected: %c got: %c", type ?: '?', have ?: '?');
    return have == type;
}

static GHashTable *
sailjail_application_info(DBusConnection *con, const char *desktop)
{
    GHashTable *info = NULL;
    DBusError err = DBUS_ERROR_INIT;
    DBusMessage *req = NULL;
    DBusMessage *rsp = NULL;

    if (!(req = dbus_message_new_method_call(PERMISSIONMGR_SERVICE,
                                             PERMISSIONMGR_OBJECT,
                                             PERMISSIONMGR_INTERFACE,
                                             PERMISSIONMGR_METHOD_GET_APPINFO))) {
        error("failed to create dbus method call");
        goto EXIT;
    }

    if (!dbus_message_append_args(req, DBUS_TYPE_STRING, &desktop, DBUS_TYPE_INVALID)) {
        error("failed to add method call args");
        goto EXIT;
    }

    if (!(rsp = dbus_connection_send_with_reply_and_block(con, req, DBUS_TIMEOUT_INFINITE, &err))) {
        if (strcmp(err.name, CODE_INVALID_ARGS) ||
                strncmp(err.message, ERROR_INVALID_APPNAME, strlen(ERROR_INVALID_APPNAME))) {
            error("method call failed: %s: %s", err.name, err.message);
        }
        goto EXIT;
    }

    if (dbus_set_error_from_message(&err, rsp)) {
        error("error reply received: %s: %s", err.name, err.message);
        goto EXIT;
    }

    DBusMessageIter bodyIter;
    if (!dbus_message_iter_init(rsp, &bodyIter)) {
        error("empty reply received");
        goto EXIT;
    }

    if (!iter_at(&bodyIter, DBUS_TYPE_ARRAY)) {
        error("reply is not an array");
        goto EXIT;
    }

    info = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_variant_unref);

    DBusMessageIter ArrayIter;
    dbus_message_iter_recurse(&bodyIter, &ArrayIter);
    while (!iter_at(&ArrayIter, DBUS_TYPE_INVALID)) {
        if (!iter_at(&ArrayIter, DBUS_TYPE_DICT_ENTRY)) {
            error("reply is not an array of dict entries");
            goto EXIT;
        }
        DBusMessageIter dictIter;
        dbus_message_iter_recurse(&ArrayIter, &dictIter);
        dbus_message_iter_next(&ArrayIter);
        if (!iter_at(&dictIter, DBUS_TYPE_STRING)) {
            error("key is not a string");
            goto EXIT;
        }
        const char *key = NULL;
        dbus_message_iter_get_basic(&dictIter, &key);
        dbus_message_iter_next(&dictIter);
        if (!iter_at(&dictIter, DBUS_TYPE_VARIANT)) {
            error("values is not a variant");
            goto EXIT;
        }
        DBusMessageIter variantIter;
        dbus_message_iter_recurse(&dictIter, &variantIter);
        dbus_message_iter_next(&dictIter);
        DBusBasicValue value;
        switch (dbus_message_iter_get_arg_type(&variantIter)) {
        case DBUS_TYPE_INT32:
            dbus_message_iter_get_basic(&variantIter, &value);
            debug("%s = int32:%d", key, value.i32);
            g_hash_table_insert(info, g_strdup(key), g_variant_new_int32(value.i32));
            break;
        case DBUS_TYPE_UINT32:
            dbus_message_iter_get_basic(&variantIter, &value);
            debug("%s = uint32:%d", key, value.u32);
            g_hash_table_insert(info, g_strdup(key), g_variant_new_uint32(value.u32));
            break;
        case DBUS_TYPE_BOOLEAN:
            dbus_message_iter_get_basic(&variantIter, &value);
            debug("%s = bool:%d", key, value.bool_val);
            g_hash_table_insert(info, g_strdup(key), g_variant_new_boolean(value.bool_val));
            break;
        case DBUS_TYPE_STRING:
            dbus_message_iter_get_basic(&variantIter, &value);
            debug("%s = string:'%s'", key, value.str);
            g_hash_table_insert(info, g_strdup(key), g_variant_new_string(value.str));
            break;
        case DBUS_TYPE_ARRAY:
            if (dbus_message_iter_get_element_type(&variantIter) != DBUS_TYPE_STRING) {
                error("only arrays of strings are supported");
            } else {
                int n = dbus_message_iter_get_element_count(&variantIter);
                DBusMessageIter valueIter;
                dbus_message_iter_recurse(&variantIter, &valueIter);
                char **v = g_malloc0_n(n + 1, sizeof *v);
                int i = 0;
                while (i < n) {
                    if (!iter_at(&valueIter, DBUS_TYPE_STRING))
                        break;
                    dbus_message_iter_get_basic(&valueIter, &value);
                    debug("%s[%d] = string:'%s'", key, i, value.str);

                    dbus_message_iter_next(&valueIter);
                    v[i++] = g_strdup(value.str);
                }
                v[i] = NULL;
                g_hash_table_insert(info, g_strdup(key), g_variant_new_strv((const gchar *const *)v, i));
                g_strfreev(v);
            }
            break;
        default:
            warning("reply contains unhandled variant types");
            break;
        }
    }

EXIT:
    if (info && g_hash_table_size(info) < 1) {
        /* There should always be at least app id key. If we get
         * empty hash table, either parser is not working or there
         * are other kinds of problems.
         */
        error("no information about application '%s'", desktop);
        g_hash_table_destroy(info), info = NULL;
    }
    dbus_error_free(&err);
    if (rsp)
        dbus_message_unref(rsp);
    if (req)
        dbus_message_unref(req);
    debug("info received = %s", info ? "true" : "false");
    return info;
}

static char **
sailjail_prompt_permissions(DBusConnection *con, const char *desktop)
{
    char **granted = NULL;
    DBusError err = DBUS_ERROR_INIT;
    DBusMessage *req = NULL;
    DBusMessage *rsp = NULL;
    int len = 0;

    if (!(req = dbus_message_new_method_call(PERMISSIONMGR_SERVICE,
                                             PERMISSIONMGR_OBJECT,
                                             PERMISSIONMGR_INTERFACE,
                                             PERMISSIONMGR_METHOD_PROMPT))) {
        error("failed to create dbus method call");
        goto EXIT;
    }

    if (!dbus_message_append_args(req,
                                  DBUS_TYPE_STRING, &desktop,
                                  DBUS_TYPE_INVALID)) {
        error("failed to add method call args");
        goto EXIT;
    }

    if (!(rsp = dbus_connection_send_with_reply_and_block(con, req, DBUS_TIMEOUT_INFINITE, &err))) {
        error("method call failed: %s: %s", err.name, err.message);
        goto EXIT;
    }

    if (dbus_set_error_from_message(&err, rsp)) {
        error("error reply received: %s: %s", err.name, err.message);
        goto EXIT;
    }

    if (!dbus_message_get_args(rsp, &err,
                               DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &granted, &len,
                               DBUS_TYPE_INVALID)) {
        error("parsing reply failed: %s: %s", err.name, err.message);
        dbus_free_string_array(granted), granted = NULL;
        goto EXIT;
    }

EXIT:
    dbus_error_free(&err);
    if (rsp)
        dbus_message_unref(rsp);
    if (req)
        dbus_message_unref(req);

    info("launch permitted = %s", granted ? "true" : "false");
    return granted;
}

static int
sailjailclient_get_field_code(const char *arg)
{
    // Non-null string starting with a '%' followed by exactly one character
    return arg && arg[0] == '%' && arg[1] && !arg[2] ? arg[1] : 0;
}

static bool
sailjailclient_is_option(const char *arg)
{
    // Non-null string starting with a hyphen
    return arg && arg[0] == '-';
}

static bool
sailjailclient_ignore_arg(const char *arg)
{
    return !g_strcmp0(arg, "-prestart");
}

static bool
sailjailclient_match_argv(const char **tpl_argv, const char **app_argv)
{
    bool matching = false;

    /* Match each arg in template */
    for (;;) {
        const char *want = *tpl_argv++;

        /* Allow some slack e.g. regarding "-prestart" options */
        while (*app_argv && g_strcmp0(*app_argv, want) &&
               sailjailclient_ignore_arg(*app_argv)) {
            warning("ignoring argument: %s", *app_argv);
            ++app_argv;
        }

        if (!want) {
            /* Template args exhausted */
            if (*app_argv) {
                /* Excess application args */
                info("argv has unwanted '%s'", *app_argv);
                goto EXIT;
            }
            break;
        }

        int field_code = sailjailclient_get_field_code(want);

        if (!field_code) {
            /* Exact match needed */
            if (g_strcmp0(*app_argv, want)) {
                /* Application args has something else */
                info("argv is missing '%s'", want);
                goto EXIT;
            }
            ++app_argv;
            continue;
        }

        /* Field code explanations from "Desktop Entry Specification"
         *
         * https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html#exec-variables
         */
        int code_args = 0;
        switch (field_code) {
        case 'f': /* A single file name (or none) */
        case 'u': /* A single URL (or none) */
            code_args = -1;
            break;
        case 'c': /* The translated name of the application */
        case 'k': /* The location of the desktop file */
            code_args = 1;
            break;
        case 'F': /* A list of files */
        case 'U': /* A list of URLs */
            code_args = INT_MIN;
            break;
        case 'i':
            /* The Icon key of the desktop entry expanded as two
             * arguments, first --icon and then the value of the
             * Icon key. Should not expand to any arguments if
             * the Icon key is empty or missing.
             */
            if (!g_strcmp0(*app_argv, "--icon"))
                ++app_argv, code_args = 1;
            break;
        case 'd':
        case 'D':
        case 'n':
        case 'N':
        case 'v':
        case 'm':
            /* Deprecated */
            error("Exec line has deprecated field code '%s'", want);
            goto EXIT;
        default:
            /* Unknown */
            error("Exec line has unknown field code '%s'", want);
            goto EXIT;
        }

        if (code_args < 0) {
            /* Variable number of args */
            if (sailjailclient_get_field_code(*tpl_argv)) {
                info("Can't validate '%s %s' combination", want, *tpl_argv);
                goto EXIT;
            }
            for (; code_args < 0; ++code_args) {
                if (!*app_argv || !g_strcmp0(*app_argv, *tpl_argv))
                    break;
                if (sailjailclient_is_option(*app_argv)) {
                    info("option '%s' at field code '%s'", *app_argv, want);
                    goto EXIT;
                }
                ++app_argv;
            }
        } else {
            /* Specified number of args */
            for (; code_args > 0; --code_args) {
                if (!*app_argv) {
                    info("missing args for field code '%s'", want);
                    goto EXIT;
                }
                if (sailjailclient_is_option(*app_argv)) {
                    info("option '%s' at field code '%s'", *app_argv, want);
                    goto EXIT;
                }
                ++app_argv;
            }
        }
    }

    matching = true;

EXIT:
    return matching;
}

static bool
sailjailclient_validate_argv(const char *exec, const gchar **app_argv)
{
    bool validated = false;
    GError *err = NULL;
    gchar **exec_argv = NULL;

    if( !exec ) {
        /* No exec line provided -> invalid */
        goto EXIT;
    }

    if (!app_argv || !*app_argv) {
        error("application argv not defined");
        goto EXIT;
    }

    /* Split desktop Exec line into argv */
    if (!g_shell_parse_argv(exec, NULL, &exec_argv, &err)) {
        error("Exec line parse failure: %s", err->message);
        goto EXIT;
    }

    if (!exec_argv || !*exec_argv) {
        error("Exec line not defined");
        goto EXIT;
    }

    /* Expectation: Exec line might have leading 'wrapper' executables
     * such as sailjail, invoker, etc -> make an attempt to skip those
     * by looking for argv[0] for command we are about to launch.
     *
     * App may also be defined without absolute path, in which case it
     * must reside in BINDIR and it must not have any 'wrapper'
     * executables. Thus check the path of app_argv[0] and compare Exec
     * line to the binary name.
     */
    const char **tpl_argv = (const char **)exec_argv;
    if (!path_dirname_eq(app_argv[0], BINDIR) ||
            g_strcmp0(*tpl_argv, path_basename(app_argv[0]))) {
        /* App is not specified without path as the first argument,
         * => there might be 'wrappers' and we match to full path.
         */
        for( ; *tpl_argv; ++tpl_argv ) {
            if( !g_strcmp0(*tpl_argv, app_argv[0]) )
                break;
        }

        if( !*tpl_argv ) {
            error("Exec line does not contain '%s'", *app_argv);
            goto EXIT;
        }
    }

    /* Argument zero has been checked already */
    if (!sailjailclient_match_argv(tpl_argv + 1, app_argv + 1)) {
        /* Leave error reporting to caller */
        goto EXIT;
    }

    validated = true;

EXIT:
    g_strfreev(exec_argv);
    g_clear_error(&err);

    return validated;
}

bool
sailjail_verify_launch(const char *desktop, const char **argv)
{
    bool allowed = false;
    DBusConnection *con = NULL;
    char **granted = NULL;
    GHashTable *info = NULL;
    const char **requested = NULL;
    const char *exec = NULL;
    const char *exec_dbus = NULL;

    if (!(con = sailjail_connect_bus()))
        goto EXIT;

    if (!(info = sailjail_application_info(con, desktop)))
        goto EXIT;

    if (!(exec = appinfo_string(info, DESKTOP_KEY_EXEC))) {
        error("no Exec line defined for application '%s'", desktop);
        goto EXIT;
    }

    exec_dbus = appinfo_string(info, SAILJAIL_KEY_EXEC_DBUS);

    if (!sailjailclient_validate_argv(exec, argv) &&
        !sailjailclient_validate_argv(exec_dbus, argv)) {
        gchar *args = g_strjoinv(" ", (gchar **)argv);
        error("Application args do not match Exec line template%s", exec_dbus ? "s" : "");
        error("exec: %s", exec);
        if( exec_dbus )
            error("exec dbus: %s", exec_dbus);
        error("args: %s", args);
        g_free(args);
        goto EXIT;
    }

    if (!(requested = appinfo_strv(info, SAILJAIL_KEY_PERMISSIONS))) {
        error("no permissions defined for application '%s'", desktop);
        goto EXIT;
    }

    debug("prompting permissions for application '%s'", desktop);
    if (!(granted = sailjail_prompt_permissions(con, desktop)))
        goto EXIT;

    for (int i = 0; requested[i]; ++i) {
        if (!g_strv_contains((const gchar *const *)granted, requested[i])) {
            error("application '%s' has not been granted '%s' permission",
                  desktop, requested[i]);
            goto EXIT;
        }
    }

    allowed = true;

EXIT:
    if (info)
        g_hash_table_destroy(info);
    g_strfreev(granted);
    sailjail_disconnect_bus(con);

    return allowed;
}

bool sailjail_sandbox(const char *desktop)
{
    bool sandboxed = false;
    DBusConnection *con = NULL;
    GHashTable *info = NULL;
    const char *mode;

    if (!(con = sailjail_connect_bus()))
        goto EXIT;

    if (!(info = sailjail_application_info(con, desktop)))
        goto EXIT;

    if ((mode = appinfo_string(info, SAILJAIL_KEY_MODE)) && g_strcmp0(mode, "None")) {
        // Mode is either "Normal" or "Compatibility"
        sandboxed = true;
    }

EXIT:
    if (info)
        g_hash_table_destroy(info);
    sailjail_disconnect_bus(con);

    return sandboxed;
}
