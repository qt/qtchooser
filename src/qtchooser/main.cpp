/****************************************************************************
**
** Copyright (C) 2014 Intel Corporation.
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

/*
 * This tool is meant to wrap the other Qt tools by searching for different
 * instances of Qt on the system. It's mostly destined to be used on Unix
 * systems other than Mac OS X. For that reason, it uses POSIX APIs and does
 * not try to be compatible with the DOS or Win32 APIs. In particular, it uses
 * opendir(3) and readdir(3) as well as paths exclusively separated by slashes.
 */

#define _CRT_SECURE_NO_WARNINGS
#define _POSIX_C_SOURCE 200809L

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#if defined(_WIN32) || defined(__WIN32__)
#  include <process.h>
#  define execv _execv
#  define stat _stat
#  define PATH_SEP "\\"
#  define EXE_SUFFIX ".exe"
#else
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <dirent.h>
#  include <fcntl.h>
#  include <libgen.h>
#  include <pwd.h>
#  include <unistd.h>
#  define PATH_SEP "/"
#  define EXE_SUFFIX ""
#endif

using namespace std;

static const char myName[] = "qtchooser" EXE_SUFFIX;
static const char confSuffix[] = ".conf";

static const char *argv0;
enum Mode {
    Unknown,
    PrintHelp,
    RunTool,
    ListVersions,
    PrintEnvironment,
    Install
};

enum InstallOptions
{
    GlobalInstall    = 0,
    LocalInstall     = 1,

    NoOverwrite      = 0,
    ForceOverwrite   = 2
};

struct Sdk
{
    string name;
    string configFile;
    string toolsPath;
    string librariesPath;

    bool isValid() const { return !toolsPath.empty(); }
    bool hasTool(const string &targetTool) const;
};

bool Sdk::hasTool(const string &targetTool) const
{
    struct stat st;
    if (toolsPath.empty())
        return false;
    if (stat((toolsPath + PATH_SEP + targetTool).c_str(), &st))
        return false;
#ifdef S_IEXEC
    return (st.st_mode & S_IEXEC);
#endif
    return true;
}

struct ToolWrapper
{
    int printHelp();
    int listVersions();
    int printEnvironment(const string &targetSdk);
    int runTool(const string &targetSdk, const string &targetTool, char **argv);
    int install(const string &sdkName, const string &qmake, int installOptions);

private:
    vector<string> searchPaths() const;

    typedef bool (*VisitFunction)(const string &targetSdk, Sdk &item);
    typedef void (*FinishFunction)(const set<string> &seenSdks);
    Sdk iterateSdks(const string &targetSdk, VisitFunction visit, FinishFunction finish = 0,
                    const string &targetTool = "");
    Sdk selectSdk(const string &targetSdk, const string &targetTool = "");

    static void printSdks(const set<string> &seenNames);
    static bool matchSdk(const string &targetSdk, Sdk &sdk);
};

int ToolWrapper::printHelp()
{
    puts("Usage:\n"
         "  qtchooser { -l | -list-versions | -print-env }\n"
         "  qtchooser -install [-f] [-local] <name> <path-to-qmake>\n"
         "  qtchooser -run-tool=<tool name> [-qt=<Qt version>] [program arguments]\n"
         "  <executable name> [-qt=<Qt version>] [program arguments]\n"
         "\n"
         "Environment variables accepted:\n"
         " QTCHOOSER_RUNTOOL  name of the tool to be run (same as the -run-tool argument)\n"
         " QT_SELECT          version of Qt to be run (same as the -qt argument)\n");
    return 0;
}

int ToolWrapper::listVersions()
{
    iterateSdks(string(), 0, &ToolWrapper::printSdks);
    return 0;
}

int ToolWrapper::printEnvironment(const string &targetSdk)
{
    Sdk sdk = selectSdk(targetSdk);
    if (!sdk.isValid())
        return 1;

    // ### The paths and the name are not escaped, so hopefully no one will be
    // installing Qt on paths containing backslashes or quotes on Unix. Paths
    // with spaces are taken into account by surrounding the arguments with
    // quotes.

    printf("QT_SELECT=\"%s\"\n", sdk.name.c_str());
    printf("QTTOOLDIR=\"%s\"\n", sdk.toolsPath.c_str());
    printf("QTLIBDIR=\"%s\"\n", sdk.librariesPath.c_str());
    return 0;
}

static string userHome()
{
    const char *value = getenv("HOME");
    if (value)
        return value;

#if defined(_WIN32) || defined(__WIN32__)
    // ### FIXME: some Windows-specific code to get the user's home directory
    // using GetUserProfileDirectory (userenv.h / dll)
    return "C:";
#else
    struct passwd *pwd = getpwuid(getuid());
    if (pwd && pwd->pw_dir)
        return pwd->pw_dir;
    return string();
#endif
}

static inline bool beginsWith(const char *haystack, const char *needle)
{
    return strncmp(haystack, needle, strlen(needle)) == 0;
}

static inline bool endsWith(const char *haystack, const char *needle)
{
    size_t haystackLen = strlen(haystack);
    size_t needleLen = strlen(needle);
    if (needleLen > haystackLen)
        return false;
    return strcmp(haystack + haystackLen - needleLen, needle) == 0;
}

bool linksBackToSelf(const char *link, const char *target)
{
#if !defined(_WIN32) && !defined(__WIN32__)
    char buf[MAXPATHLEN], buf2[MAXPATHLEN];
    int count = readlink(realpath(link, buf2), buf, sizeof(buf) - 1);
    if (count >= 0) {
        buf[count] = '\0';
        if (endsWith(buf, target)) {
            fprintf(stderr, "%s: could not exec '%s' since it links to %s itself. Check your installation.\n",
                    target, link, target);
            return true;
        }
    }
#endif
    return false;
}

static bool readLine(FILE *f, string *result)
{
#if _POSIX_VERSION >= 200809L
    size_t len = 0;
    char *line = 0;
    ssize_t read = getline(&line, &len, f);
    if (read < 0) {
        free(line);
        return false;
    }

    line[strlen(line) - 1] = '\0';
    *result = line;
    free(line);
#elif defined(PATH_MAX)
    char buf[PATH_MAX];
    if (!fgets(buf, PATH_MAX - 1, f))
        return false;

    buf[PATH_MAX - 1] = '\0';
    buf[strlen(buf) - 1] = '\0';
    *result = buf;
#else
# error "POSIX < 2008 and no PATH_MAX, fix me"
#endif
    return true;
}

static bool mkparentdir(string name)
{
    // create the dir containing this dir
    size_t pos = name.rfind('/');
    if (pos == string::npos)
        return false;
    name.erase(pos);
    if (mkdir(name.c_str(), 0777) == -1) {
        if (errno != ENOENT)
            return false;
        // try this dir's parent too
        if (!mkparentdir(name))
            return false;
    }
    return true;
}

int ToolWrapper::runTool(const string &targetSdk, const string &targetTool, char **argv)
{
    Sdk sdk = selectSdk(targetSdk, targetTool);
    if (!sdk.isValid())
        return 1;

    string tool = sdk.toolsPath + PATH_SEP + targetTool;
    if (tool[0] == '~')
        tool = userHome() + tool.substr(1);

    // check if the tool is a symlink to ourselves
    if (linksBackToSelf(tool.c_str(), argv0))
        return 1;

    argv[0] = &tool[0];
#ifdef QTCHOOSER_TEST_MODE
    while (*argv)
        printf("%s\n", *argv++);
    return 0;
#else
    execv(argv[0], argv);
#ifdef __APPLE__
    // failed; see if we have a .app package by the same name
    {
        char appPath[PATH_MAX];
        snprintf(appPath, PATH_MAX, "%s.app/Contents/MacOS/%s",
                 argv[0], basename(argv[0]));
        execv(appPath, argv);
    }
#endif
    fprintf(stderr, "%s: could not exec '%s': %s\n",
            argv0, argv[0], strerror(errno));
    return 1;
#endif
}

static const char *to_number(int number)
{
    // obviously not thread-safe
    static char buffer[sizeof "2147483647"];
    snprintf(buffer, sizeof buffer, "%d", number);
    return buffer;
}

int ToolWrapper::install(const string &sdkName, const string &qmake, int installOptions)
{
    if (qmake.size() == 0) {
        fprintf(stderr, "%s: missing option: path to qmake\n", argv0);
        return 1;
    }

    if ((installOptions & ForceOverwrite) == 0) {
        Sdk matchedSdk = iterateSdks(sdkName, &ToolWrapper::matchSdk);
        if (matchedSdk.isValid()) {
            fprintf(stderr, "%s: SDK \"%s\" already exists\n", argv0, sdkName.c_str());
            return 1;
        }
    }

    // first of all, get the bin and lib dirs from qmake
    string bindir, libdir;
    FILE *bin, *lib;
    bin = popen(("'" + qmake + "' -query QT_INSTALL_BINS").c_str(), "r");
    lib = popen(("'" + qmake + "' -query QT_INSTALL_LIBS").c_str(), "r");

    if (!readLine(bin, &bindir) || !readLine(lib, &libdir)\
            || pclose(bin) == -1 || pclose(lib) == -1) {
        fprintf(stderr, "%s: error running %s: %s\n", argv0, qmake.c_str(), strerror(errno));
        return 1;
    }

    const string sdkFileName = sdkName + confSuffix;
    const string fileContents = bindir + "\n" + libdir + "\n";
    string sdkFullPath;

    // get the list of paths to try and install the SDK on the first we are able to;
    // since the list is sorted in search order, we need to try in the reverse order
    const vector<string> paths = searchPaths();
    vector<string>::const_iterator it = paths.end();
    vector<string>::const_iterator prev = it - 1;
    for ( ; it != paths.begin(); it = prev--) {
        sdkFullPath = *prev + sdkFileName;

        // are we trying to install here?
        bool installHere = (installOptions & LocalInstall) == 0 || prev == paths.begin();
        if (!installHere)
            continue;

#ifdef QTCHOOSER_TEST_MODE
        puts(sdkFullPath.c_str());
        puts(fileContents.c_str());
        return 0;
#else
        // we're good, create the SDK name
        string tempname = sdkFullPath + "." + to_number(rand());
        int fd;
        // create a temporary file here
        while (true) {
            fd = ::open(tempname.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
            if (fd == -1 && errno == EEXIST)
                continue;
            if (fd == -1 && errno == ENOENT) {
                // could be because the dir itself doesn't exist
                if (mkparentdir(tempname))
                    continue;
            }
            break;
        }
        if (fd == -1)
            continue;

        size_t bytesWritten = 0;
        while (bytesWritten < fileContents.size()) {
            ssize_t written = ::write(fd, fileContents.data() + bytesWritten, fileContents.size() - bytesWritten);
            if (written == -1) {
                fprintf(stderr, "%s: error writing to \"%s\": %s\n", argv0, tempname.c_str(), strerror(errno));
                ::close(fd);
                return 1;
            }

            bytesWritten += written;
        }

        // atomic rename
        ::close(fd);
        if (rename(tempname.c_str(), sdkFullPath.c_str()) == 0)
            return 0;   // success
#endif
    }

    // if we got here, we failed to create the file
    fprintf(stderr, "%s: could not create SDK: %s: %s\n", argv0, sdkFullPath.c_str(), strerror(errno));
    return 1;
}

static vector<string> stringSplit(const char *source)
{
#if defined(_WIN32) || defined(__WIN32__)
    char listSeparator = ';';
#else
    char listSeparator = ':';
#endif

    vector<string> result;
    if (!*source)
        return result;

    while (true) {
        const char *p = strchr(source, listSeparator);
        if (!p) {
            result.push_back(source);
            return result;
        }

        result.push_back(string(source, p - source));
        source = p + 1;
    }
    return result;
}

static string qgetenv(const char *env, const string &defaultValue = string())
{
    const char *value = getenv(env);
    return value ? string(value) : defaultValue;
}

vector<string> ToolWrapper::searchPaths() const
{
    vector<string> paths;

    string localDir = qgetenv("XDG_CONFIG_HOME", userHome() + PATH_SEP ".config");
    paths.push_back(localDir);

    // search the XDG config location directories
    vector<string> xdgPaths = stringSplit(qgetenv("XDG_CONFIG_DIRS", "/etc/xdg").c_str());
    paths.insert(paths.end(), xdgPaths.begin(), xdgPaths.end());

#if defined(QTCHOOSER_GLOBAL_DIR)
    if (qgetenv("QTCHOOSER_NO_GLOBAL_DIR").empty()) {
        vector<string> globalPaths = stringSplit(QTCHOOSER_GLOBAL_DIR);
        paths.insert(paths.end(), globalPaths.begin(), globalPaths.end());
    }
#endif

    for (vector<string>::iterator it = paths.begin(); it != paths.end(); ++it)
        *it += "/qtchooser/";

    return paths;
}

Sdk ToolWrapper::iterateSdks(const string &targetSdk, VisitFunction visit, FinishFunction finish,
                             const string &targetTool)
{
    vector<string> paths = searchPaths();
    set<string> seenNames;
    Sdk sdk;
    for (vector<string>::iterator it = paths.begin(); it != paths.end(); ++it) {
        const string &path = *it;

        // no ISO C++ or ISO C API for listing directories, so use POSIX
        DIR *dir = opendir(path.c_str());
        if (!dir)
            continue;  // no such dir or not a dir, doesn't matter

        while (struct dirent *d = readdir(dir)) {
#ifdef _DIRENT_HAVE_D_TYPE
            if (d->d_type == DT_DIR)
                continue;
#endif

            size_t fnamelen = strlen(d->d_name);
            if (fnamelen < sizeof(confSuffix))
                continue;
            if (memcmp(d->d_name + fnamelen + 1 - sizeof(confSuffix), confSuffix, sizeof confSuffix - 1) != 0)
                continue;

            if (seenNames.find(d->d_name) != seenNames.end())
                continue;

            seenNames.insert(d->d_name);
            if (targetTool.empty()) {
                sdk.name = d->d_name;
                sdk.name.resize(fnamelen + 1 - sizeof confSuffix);
            } else {
                // To make the check in matchSdk() succeed
                sdk.name = "default";
            }
            sdk.configFile = path + PATH_SEP + d->d_name;
            if (visit && visit(targetSdk, sdk)) {
                // If a tool was requested, but not found here, skip this sdk
                if (!targetTool.empty() && !sdk.hasTool(targetTool))
                    continue;
                return sdk;
            }
        }

        closedir(dir);
    }

    if (finish)
        finish(seenNames);

    return Sdk();
}

// All tools that exist for only one Qt version should be
// here. Other tools in this list are qdbus and qmlscene.
bool fallbackAllowed(const string &tool)
{
    return tool == "qdbus" ||
           tool == "qml" ||
           tool == "qmlimportscanner" ||
           tool == "qmlscene" ||
           tool == "qtdiag" ||
           tool == "qtpaths" ||
           tool == "qtplugininfo";
}

Sdk ToolWrapper::selectSdk(const string &targetSdk, const string &targetTool)
{
    // First, try the requested SDK
    Sdk matchedSdk = iterateSdks(targetSdk, &ToolWrapper::matchSdk);
    if (targetSdk.empty() && !matchedSdk.hasTool(targetTool) && fallbackAllowed(targetTool)) {
        // If a tool was requested, fall back to any SDK that has it
        matchedSdk = iterateSdks(string(), &ToolWrapper::matchSdk, 0, targetTool);
    }
    if (!matchedSdk.isValid()) {
        fprintf(stderr, "%s: could not find a Qt installation of '%s'\n", argv0, targetSdk.c_str());
    }
    return matchedSdk;
}

void ToolWrapper::printSdks(const set<string> &seenNames)
{
    vector<string> sorted;
    copy(seenNames.begin(), seenNames.end(), back_inserter(sorted));
    sort(sorted.begin(), sorted.end());
    vector<string>::const_iterator it = sorted.begin();
    for ( ; it != sorted.end(); ++it) {
        // strip the .conf suffix
        string s = *it;
        s.resize(s.size() - sizeof confSuffix + 1);
        printf("%s\n", s.c_str());
    }
}

bool ToolWrapper::matchSdk(const string &targetSdk, Sdk &sdk)
{
    if (targetSdk == sdk.name || (targetSdk.empty() && sdk.name == "default")) {
        FILE *f = fopen(sdk.configFile.c_str(), "r");
        if (!f) {
            fprintf(stderr, "%s: could not open config file '%s': %s\n",
                    argv0, sdk.configFile.c_str(), strerror(errno));
            exit(1);
        }

        // read the first two lines.
        // 1) the first line contains the path to the Qt tools like qmake
        // 2) the second line contains the path to the Qt libraries
        // further lines are reserved for future enhancement
        if (!readLine(f, &sdk.toolsPath) || !readLine(f, &sdk.librariesPath)) {
            fclose(f);
            return false;
        }

        fclose(f);
        return true;
    }

    return false;
}

int main(int argc, char **argv)
{
    // search the environment for defaults
    Mode operatingMode = Unknown;
    argv0 = basename(argv[0]);
    const char *targetSdk = getenv("QT_SELECT");

    // the default tool is the one in argv[0]
    const char *targetTool = argv0;

    // if argv[0] points back to us, use the environment
    if (strcmp(targetTool, myName) == 0)
        targetTool = getenv("QTCHOOSER_RUNTOOL");
    else
        operatingMode = RunTool;

    // check the arguments to see if there's an override
    int optind = 1;
    for ( ; optind < argc; ++optind) {
        char *arg = argv[optind];
        if (*arg == '-') {
            ++arg;
            if (*arg == '-')
                ++arg;
            if (!*arg) {
                // -- argument, ends our processing
                // but skip this dash-dash argument
                ++optind;
                break;
            } else if (beginsWith(arg, "qt")) {
                // -qtX or -qt=X argument
                arg += 2;
                targetSdk = *arg == '=' ? arg + 1 : arg;
            } else if (!targetTool && beginsWith(arg, "run-tool=")) {
                // -run-tool= argument
                targetTool = arg + strlen("run-tool=");
                operatingMode = RunTool;
            } else {
                // not one of our arguments, must be for the target tool
                break;
            }
        } else {
            // not one of our arguments, must be for the target tool
            break;
        }
    }

    if (!targetSdk)
        targetSdk = "";

    ToolWrapper wrapper;
    if (operatingMode == RunTool || targetTool) {
        if (!targetTool) {
            fprintf(stderr, "%s: no tool selected. Stop.\n", argv0);
            return 1;
        }
        return wrapper.runTool(targetSdk,
                               targetTool,
                               argv + optind - 1);
    }

    // running qtchooser itself
    // check for our arguments
    operatingMode = PrintHelp;
    int installOptions = 0;
    string sdkName;
    string qmakePath;
    for ( ; optind < argc; ++optind) {
        char *arg = argv[optind];
        if (*arg == '-') {
            ++arg;
            if (*arg == '-')
                ++arg;
            // double-dash arguments are OK too
            if (*arg == '-')
                ++arg;
            if (strcmp(arg, "install") == 0) {
                operatingMode = Install;
            } else if (operatingMode == Install && (strcmp(arg, "force") == 0 || strcmp(arg, "f") == 0)) {
                installOptions |= ForceOverwrite;
            } else if (strcmp(arg, "list-versions") == 0 || strcmp(arg, "l") == 0) {
                operatingMode = ListVersions;
            } else if (operatingMode == Install && strcmp(arg, "local") == 0) {
                installOptions |= LocalInstall;
            } else if (beginsWith(arg, "print-env")) {
                operatingMode = PrintEnvironment;
            } else if (strcmp(arg, "help") != 0) {
                fprintf(stderr, "%s: unknown option: %s\n", argv0, arg - 1);
                return 1;
            }
        } else if (operatingMode == Install) {
            if (qmakePath.size()) {
                fprintf(stderr, "%s: install mode takes exactly two arguments; unknown option: %s\n", argv0, arg);
                return 1;
            }
            if (sdkName.size()) {
                qmakePath = arg;
            } else {
                sdkName = strlen(arg) ? arg : "default";
            }
        } else {
            fprintf(stderr, "%s: unknown argument: %s\n", argv0, arg);
            return 1;
        }
    }

    // dispatch
    switch (operatingMode) {
    case RunTool:
    case Unknown:
        // can't happen!
        assert(false);
        return 127;

    case PrintHelp:
        return wrapper.printHelp();

    case PrintEnvironment:
        return wrapper.printEnvironment(targetSdk);

    case ListVersions:
        return wrapper.listVersions();

    case Install:
        return wrapper.install(sdkName, qmakePath, installOptions);
    }
}
