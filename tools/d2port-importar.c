/*
 * d2port-importar: copia al paquete del port los archivos del juego que
 * necesita, sacándolos de tu propia instalación de Diablo II: Resurrected
 * (el almacenamiento CASC, con CascLib). El port no trae ningún archivo de
 * Blizzard: cada uno los pone desde su copia del juego.
 *
 *   d2port-importar [--idioma es|en] [--juego <carpeta del D2R>] [--destino <carpeta>] [--sin-pausa]
 *
 * Sin argumentos pregunta lo que falta. El destino es la carpeta `ports` del
 * paquete (o su `.data/d2port`): ahí está `archivos.txt`, la lista de lo que
 * usa el port. También puede ser la carpeta del código del port (la del
 * repositorio, con CMakeLists.txt): la lista es tools/archivos-del-juego.txt
 * y los archivos van a su `data/`, que git no sube, para compilarlo y
 * jugarlo desde el código. Solo se copian los de la lista, no el juego
 * entero. Las voces salen del idioma elegido (las del español latino, de
 * locales/audio/esmx) y el idioma queda anotado en `idioma.txt`, que el
 * juego lee al arrancar.
 *
 * Compila para Windows (mingw), Linux y la Pi (aarch64); ver
 * scripts/package-release.sh.
 */
#include <CascLib.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(p) _mkdir(p)
#else
#include <dirent.h>
#include <glob.h>
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

static int s_en; /* los mensajes en inglés */

#define T(es, en) (s_en ? (en) : (es))

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static void chomp(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '"'))
        s[--n] = '\0';
    if (s[0] == '"') /* (arrastrar una carpeta a la consola la pone entre comillas) */
        memmove(s, s + 1, strlen(s));
}

static void ask(const char *prompt, char *out, size_t cap)
{
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(out, (int)cap, stdin))
        out[0] = '\0';
    chomp(out);
}

static void join(char *out, size_t cap, const char *a, const char *b)
{
    size_t n = strlen(a);
    if (snprintf(out, cap, "%s%s%s", a, n && (a[n - 1] == '/' || a[n - 1] == '\\') ? "" : "/", b) >= (int)cap)
        out[0] = '\0'; /* (una ruta de más de 1 KB: mejor que no exista a que sea otra) */
}

/* "data:data/global/excel/armor.txt" -> "data/global/excel/armor.txt", en minúsculas */
static void normalize(const char *in, char *out, size_t cap)
{
    const char *colon = strchr(in, ':');
    if (colon)
        in = colon + 1;
    size_t i = 0;
    for (; *in && i + 1 < cap; in++, i++)
        out[i] = *in == '\\' ? '/' : (char)tolower((unsigned char)*in);
    out[i] = '\0';
}

static int mkdirs_for(const char *path)
{
    char tmp[2048];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/' && *p != '\\')
            continue;
        char c = *p;
        *p = '\0';
        /* (el error de cada uno no importa: "C:" o el servidor de una ruta de
           red como \\BATOCERA\share no se crean; si falta algo, falla al
           abrir el archivo) */
        if (!(p > tmp && p[-1] == ':'))
            MKDIR(tmp);
        *p = c;
    }
    return 0;
}

/* ---- La lista de archivos (archivos.txt) en una tabla hash ---- */

typedef struct {
    char *path;      /* "data/..." como lo busca el port */
    char *casc_name; /* de dónde sale en el CASC */
    int   prio;      /* 1 = la ruta de siempre, 2 = la del idioma */
} Entry;

static Entry   *s_entries;
static int      s_num;
static int     *s_table;
static unsigned s_mask;

static unsigned hash_str(const char *s)
{
    unsigned h = 2166136261u;
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

static Entry *lookup(const char *path)
{
    for (unsigned i = hash_str(path) & s_mask; s_table[i] >= 0; i = (i + 1) & s_mask)
        if (strcmp(s_entries[s_table[i]].path, path) == 0)
            return &s_entries[s_table[i]];
    return NULL;
}

static int load_list(const char *file)
{
    FILE *f = fopen(file, "r");
    if (!f)
        return -1;
    int cap = 1024;
    s_entries = malloc((size_t)cap * sizeof *s_entries);
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        chomp(line);
        if (!line[0])
            continue;
        if (s_num == cap) {
            cap *= 2;
            s_entries = realloc(s_entries, (size_t)cap * sizeof *s_entries);
        }
        char norm[1024];
        normalize(line, norm, sizeof norm);
        s_entries[s_num].path = strdup(norm);
        s_entries[s_num].casc_name = NULL;
        s_entries[s_num].prio = 0;
        s_num++;
    }
    fclose(f);
    unsigned size = 1;
    while (size < (unsigned)s_num * 2u)
        size *= 2;
    s_mask = size - 1;
    s_table = malloc(size * sizeof *s_table);
    memset(s_table, 0xff, size * sizeof *s_table);
    for (int k = 0; k < s_num; k++) {
        unsigned i = hash_str(s_entries[k].path) & s_mask;
        while (s_table[i] >= 0)
            i = (i + 1) & s_mask;
        s_table[i] = k;
    }
    return 0;
}

/* ---- Dónde está cada cosa ---- */

static int is_game_dir(const char *dir)
{
    char p[1024];
    join(p, sizeof p, dir, ".build.info");
    return exists(p);
}

static int find_game(char *out, size_t cap)
{
    static const char *const k_dirs[] = {
#ifdef _WIN32
        "C:\\Program Files (x86)\\Diablo II Resurrected",
        "C:\\Program Files\\Diablo II Resurrected",
        "C:\\Battle.net\\Diablo II Resurrected",
        "D:\\Battle.net\\Diablo II Resurrected",
        "D:\\Diablo II Resurrected",
        "E:\\Battle.net\\Diablo II Resurrected",
        "E:\\Diablo II Resurrected",
        "D:\\Games\\Diablo II Resurrected",
        "C:\\Games\\Diablo II Resurrected",
#else
        "/mnt/c/Program Files (x86)/Diablo II Resurrected",
        "/mnt/d/Battle.net/Diablo II Resurrected",
        "/mnt/d/Diablo II Resurrected",
#endif
    };
#ifdef _WIN32
    for (size_t i = 0; i < sizeof k_dirs / sizeof k_dirs[0]; i++)
        if (is_game_dir(k_dirs[i])) {
            snprintf(out, cap, "%s", k_dirs[i]);
            return 0;
        }
    /* Donde lo instaló el Battle.net */
    char loc[1024];
    DWORD size = sizeof loc;
    if (RegGetValueA(HKEY_LOCAL_MACHINE,
                     "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Diablo II Resurrected",
                     "InstallLocation", RRF_RT_REG_SZ, NULL, loc, &size) == ERROR_SUCCESS &&
        is_game_dir(loc)) {
        snprintf(out, cap, "%s", loc);
        return 0;
    }
#else
    /* En Linux, adentro del prefijo de Wine donde se instaló el Battle.net
       (Wine, Lutris, Bottles o Steam con Proton) */
    const char *home = getenv("HOME");
    static const char *const k_prefixes[] = {
        "%s/.wine",
        "%s/Games/*",
        "%s/.local/share/bottles/bottles/*",
        "%s/.var/app/com.usebottles.bottles/data/bottles/bottles/*",
        "%s/.steam/steam/steamapps/compatdata/*/pfx",
        "%s/.local/share/Steam/steamapps/compatdata/*/pfx",
        "%s/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/compatdata/*/pfx",
    };
    for (size_t i = 0; home && i < sizeof k_prefixes / sizeof k_prefixes[0]; i++) {
        static const char *const k_pf[] = { "Program Files (x86)", "Program Files" };
        for (int k = 0; k < 2; k++) {
            char prefix[512], pattern[1024];
            snprintf(prefix, sizeof prefix, k_prefixes[i], home);
            snprintf(pattern, sizeof pattern, "%s/drive_c/%s/Diablo II Resurrected", prefix, k_pf[k]);
            glob_t g;
            if (glob(pattern, 0, NULL, &g) == 0) {
                for (size_t j = 0; j < g.gl_pathc; j++)
                    if (is_game_dir(g.gl_pathv[j])) {
                        snprintf(out, cap, "%s", g.gl_pathv[j]);
                        globfree(&g);
                        return 0;
                    }
                globfree(&g);
            }
        }
    }
    /* En Batocera (o en Linux): una carpeta del juego copiada a un disco USB,
       al share o a la carpeta de ports, con cualquier nombre que diga Diablo */
    static const char *const k_parents[] = { "/media", "/userdata", "/userdata/roms/ports", "/userdata/roms",
                                             "/run/media", "/mnt" };
    for (size_t i = 0; i < sizeof k_parents / sizeof k_parents[0]; i++) {
        DIR *d = opendir(k_parents[i]);
        if (!d)
            continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.')
                continue;
            char sub[1024];
            join(sub, sizeof sub, k_parents[i], e->d_name);
            char low[256];
            normalize(e->d_name, low, sizeof low);
            if (strstr(low, "diablo") && is_game_dir(sub)) {
                snprintf(out, cap, "%s", sub);
                closedir(d);
                return 0;
            }
            /* un nivel más: /media/USB/Diablo II Resurrected */
            DIR *d2 = opendir(sub);
            if (!d2)
                continue;
            struct dirent *e2;
            while ((e2 = readdir(d2)) != NULL) {
                normalize(e2->d_name, low, sizeof low);
                char sub2[1024];
                join(sub2, sizeof sub2, sub, e2->d_name);
                if (e2->d_name[0] != '.' && strstr(low, "diablo") && is_game_dir(sub2)) {
                    snprintf(out, cap, "%s", sub2);
                    closedir(d2);
                    closedir(d);
                    return 0;
                }
            }
            closedir(d2);
        }
        closedir(d);
    }
    /* En WSL: el de Windows */
    for (size_t i = 0; i < sizeof k_dirs / sizeof k_dirs[0]; i++)
        if (is_game_dir(k_dirs[i])) {
            snprintf(out, cap, "%s", k_dirs[i]);
            return 0;
        }
#endif
    return -1;
}

/* La lista de archivos del destino: archivos.txt en el paquete,
   tools/archivos-del-juego.txt en el código */
static char s_list_name[64] = "archivos.txt";
static int  s_repo; /* el destino es el código del port */

/* La carpeta del paquete donde están d2port y archivos.txt: la que se dio
   (la de `ports` o su `.data/d2port`), o la del código del port */
static int pkg_dir(const char *dir, char *out, size_t cap)
{
    char p[1024], cm[1024];
    join(cm, sizeof cm, dir, "CMakeLists.txt");
    join(p, sizeof p, dir, "tools/archivos-del-juego.txt");
    if (exists(cm) && exists(p)) {
        snprintf(out, cap, "%s", dir);
        snprintf(s_list_name, sizeof s_list_name, "tools/archivos-del-juego.txt");
        s_repo = 1;
        return 0;
    }
    join(p, sizeof p, dir, "archivos.txt");
    if (exists(p)) {
        snprintf(out, cap, "%s", dir);
        return 0;
    }
    char sub[1024];
    join(sub, sizeof sub, dir, ".data/d2port");
    join(p, sizeof p, sub, "archivos.txt");
    if (exists(p)) {
        snprintf(out, cap, "%s", sub);
        return 0;
    }
    return -1;
}

/* Al lado del importador: <publicar>/importador/<sistema>/ -> <publicar>/batocera/ports,
   o el código si es el que se compiló en build/ */
static int find_pkg(char *out, size_t cap)
{
    char exe[1024] = "";
#ifdef _WIN32
    GetModuleFileNameA(NULL, exe, sizeof exe);
#else
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = '\0';
#endif
    char *slash = strrchr(exe, '/');
    char *bslash = strrchr(exe, '\\');
    if (bslash > slash)
        slash = bslash;
    if (!slash)
        return -1;
    *slash = '\0';
    static const char *const k_rel[] = { "../../batocera/ports", "../batocera/ports", "batocera/ports", ".", ".." };
    for (size_t i = 0; i < sizeof k_rel / sizeof k_rel[0]; i++) {
        char p[1024];
        join(p, sizeof p, exe, k_rel[i]);
        if (pkg_dir(p, out, cap) == 0)
            return 0;
    }
    return pkg_dir(".", out, cap);
}

static int extract_one(HANDLE storage, const char *casc_name, const char *out_path)
{
    HANDLE file;
    if (!CascOpenFile(storage, casc_name, 0, CASC_OPEN_BY_NAME, &file))
        return -1;
    ULONGLONG size = 0;
    if (!CascGetFileSize64(file, &size) || size > 0x7FFFFFFF) {
        CascCloseFile(file);
        return -1;
    }
    unsigned char *buf = malloc(size ? (size_t)size : 1);
    DWORD got = 0;
    int ok = buf && CascReadFile(file, buf, (DWORD)size, &got) && got == size;
    CascCloseFile(file);
    if (ok && mkdirs_for(out_path) == 0) {
        FILE *f = fopen(out_path, "wb");
        ok = f && fwrite(buf, 1, (size_t)size, f) == size;
        if (f && fclose(f) != 0)
            ok = 0;
    } else {
        ok = 0;
    }
    free(buf);
    return ok ? 0 : -1;
}

static void pause_exit(int pause)
{
    if (!pause)
        return;
    char tmp[8];
    printf("%s", T("\nPresioná Enter para salir...", "\nPress Enter to exit..."));
    fflush(stdout);
    if (!fgets(tmp, sizeof tmp, stdin))
        return;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    char lang[8] = "", game[1024] = "", dest[1024] = "", pkg[1024];
    int pause = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--idioma") == 0 && i + 1 < argc)
            snprintf(lang, sizeof lang, "%s", argv[++i]);
        else if (strcmp(argv[i], "--juego") == 0 && i + 1 < argc)
            snprintf(game, sizeof game, "%s", argv[++i]);
        else if (strcmp(argv[i], "--destino") == 0 && i + 1 < argc)
            snprintf(dest, sizeof dest, "%s", argv[++i]);
        else if (strcmp(argv[i], "--sin-pausa") == 0)
            pause = 0;
        else {
            printf("uso / usage: %s [--idioma es|en] [--juego <Diablo II Resurrected>] [--destino <ports | código / source>]"
                   " [--sin-pausa]\n",
                   argv[0]);
            return 2;
        }
    }

    printf("Diablo II port - importador / importer\n\n");
    while (strcmp(lang, "es") != 0 && strcmp(lang, "en") != 0) {
        char ans[32];
        ask("Idioma del juego / Game language:\n  1) Español\n  2) English\n> ", ans, sizeof ans);
        if (!ans[0] && feof(stdin))
            return 2;
        int c0 = tolower((unsigned char)ans[0]), c1 = tolower((unsigned char)ans[1]);
        if (strcmp(ans, "1") == 0 || (c0 == 'e' && c1 == 's'))
            strcpy(lang, "es");
        else if (strcmp(ans, "2") == 0 || (c0 == 'e' && c1 == 'n'))
            strcpy(lang, "en");
    }
    s_en = strcmp(lang, "en") == 0;

    /* El juego original */
    if (!game[0] && find_game(game, sizeof game) == 0)
        printf(T("Diablo II: Resurrected encontrado en: %s\n", "Found Diablo II: Resurrected in: %s\n"), game);
    while (!is_game_dir(game)) {
        if (game[0])
            printf(T("No está el juego en \"%s\" (falta .build.info).\n", "The game is not in \"%s\" (no .build.info).\n"), game);
        ask(T("Carpeta de Diablo II: Resurrected (podés arrastrarla acá): ",
              "Diablo II: Resurrected folder (you can drag it here): "),
            game, sizeof game);
        if (!game[0] && feof(stdin))
            return 2;
    }

    /* El paquete */
    if (dest[0] ? pkg_dir(dest, pkg, sizeof pkg) != 0 : find_pkg(pkg, sizeof pkg) != 0) {
        for (;;) {
            if (dest[0])
                printf(T("En \"%s\" no está el paquete ni el código del port (falta archivos.txt).\n",
                         "\"%s\" is neither the port package nor its source (no archivos.txt).\n"),
                       dest);
            ask(T("Carpeta `ports` del paquete (la de Batocera o la que copiaste), o la del código del port: ",
                  "The package's `ports` folder (on Batocera or where you copied it), or the port's source folder: "),
                dest, sizeof dest);
            if (!dest[0] && feof(stdin))
                return 2;
            if (pkg_dir(dest, pkg, sizeof pkg) == 0)
                break;
        }
    }
    if (s_repo)
        printf(T("Código del port: %s (los archivos van a su data/)\n", "Port source: %s (the files go to its data/)\n"), pkg);
    else
        printf(T("Paquete: %s\n", "Package: %s\n"), pkg);

    char list[1024];
    join(list, sizeof list, pkg, s_list_name);
    if (load_list(list) != 0 || s_num == 0) {
        printf(T("No pude leer %s\n", "Could not read %s\n"), list);
        pause_exit(pause);
        return 1;
    }

    HANDLE storage;
    if (!CascOpenStorage(game, CASC_LOCALE_ALL, &storage)) {
        printf(T("No pude abrir los archivos del juego en %s (error %d). ¿Está cerrado el juego y el Battle.net "
                 "terminó de instalarlo?\n",
                 "Could not open the game files in %s (error %d). Is the game closed and fully installed?\n"),
               game, (int)GetCascError());
        pause_exit(pause);
        return 1;
    }

    /* Dónde está cada uno: la ruta de siempre (las voces en inglés) o, en
       español, la de locales/audio/esmx */
    printf("%s", T("Buscando los archivos...\n", "Looking for the files...\n"));
    CASC_FIND_DATA fd;
    HANDLE find = CascFindFirstFile(storage, "*", &fd, NULL);
    if (find == INVALID_HANDLE_VALUE || find == NULL) {
        printf(T("No pude recorrer los archivos del juego (error %d).\n", "Could not list the game files (error %d).\n"),
               (int)GetCascError());
        pause_exit(pause);
        return 1;
    }
    const char *loc = "locales/audio/esmx/";
    do {
        char name[1024];
        normalize(fd.szFileName, name, sizeof name);
        if (!fd.bFileAvailable)
            continue;
        /* la del idioma elegido gana; la otra queda si es la única */
        int local = strncmp(name, loc, strlen(loc)) == 0;
        int prio = local == !s_en ? 2 : 1;
        Entry *e = lookup(local ? name + strlen(loc) : name);
        if (e && prio > e->prio) {
            free(e->casc_name);
            e->casc_name = strdup(fd.szFileName);
            e->prio = prio;
        }
    } while (CascFindNextFile(find, &fd));
    CascFindClose(find);

    /* Copiarlos */
    long done = 0, missing = 0, failed = 0;
    FILE *log = NULL;
    char log_path[1024];
    join(log_path, sizeof log_path, pkg, "importar.log");
    for (int k = 0; k < s_num; k++) {
        Entry *e = &s_entries[k];
        char out_path[2048];
        join(out_path, sizeof out_path, pkg, e->path);
        if (!e->casc_name) {
            missing++;
            if (!log)
                log = fopen(log_path, "w");
            if (log)
                fprintf(log, "falta / missing: %s\n", e->path);
            continue;
        }
        if (extract_one(storage, e->casc_name, out_path) != 0) {
            failed++;
            if (!log)
                log = fopen(log_path, "w");
            if (log)
                fprintf(log, "error: %s\n", e->path);
            continue;
        }
        if (++done % 1000 == 0)
            printf(T("  %ld de %d...\n", "  %ld of %d...\n"), done, s_num);
    }
    CascCloseStorage(storage);
    if (log)
        fclose(log);

    char lang_path[1024];
    join(lang_path, sizeof lang_path, pkg, "idioma.txt");
    FILE *lf = fopen(lang_path, "w");
    if (lf) {
        fprintf(lf, "%s\n", lang);
        fclose(lf);
    }

    printf(T("\nCopiados: %ld de %d", "\nCopied: %ld of %d"), done, s_num);
    if (missing || failed)
        printf(T(" (faltan %ld, con error %ld: ver %s)", " (%ld missing, %ld failed: see %s)"), missing, failed, log_path);
    printf("\n");
    if (!missing && !failed && s_repo)
        printf("%s", T("Listo. Ya se puede compilar y jugar desde el código:\n"
                       "  cmake -S . -B build && cmake --build build -j && ./build/d2port\n",
                       "Done. Now you can build and play from the source:\n"
                       "  cmake -S . -B build && cmake --build build -j && ./build/d2port\n"));
    else if (!missing && !failed)
        printf("%s", T("Listo. Si no importaste directo a la Pi, copiá el contenido de esta carpeta `ports` a la\n"
                       "de Batocera: /userdata/roms/ports (por la red, smb://batocera/share/roms/ports).\n",
                       "Done. If you did not import straight to the Pi, copy the contents of this `ports` folder\n"
                       "to Batocera's: /userdata/roms/ports (over the network, smb://batocera/share/roms/ports).\n"));
    pause_exit(pause);
    return missing || failed ? 1 : 0;
}
