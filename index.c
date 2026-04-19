// index.c — Staging area implementation

#include "index.h"
#include "pes.h"   // ✅ FIXED (instead of object.h)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#define INDEX_FILE ".pes/index"

// ================= PROVIDED FUNCTIONS =================

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    int staged_count = 0;
    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
        staged_count++;
    }
    if (staged_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Unstaged changes:\n");
    int unstaged_count = 0;
    for (int i = 0; i < index->count; i++) {
        struct stat st;
        if (stat(index->entries[i].path, &st) != 0) {
            printf("  deleted:    %s\n", index->entries[i].path);
            unstaged_count++;
        } else {
            if (st.st_mtime != (time_t)index->entries[i].mtime_sec || st.st_size != (off_t)index->entries[i].size) {
                printf("  modified:   %s\n", index->entries[i].path);
                unstaged_count++;
            }
        }
    }
    if (unstaged_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Untracked files:\n");
    int untracked_count = 0;
    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (strcmp(ent->d_name, ".pes") == 0) continue;
            if (strcmp(ent->d_name, "pes") == 0) continue;
            if (strstr(ent->d_name, ".o") != NULL) continue;

            int is_tracked = 0;
            for (int i = 0; i < index->count; i++) {
                if (strcmp(index->entries[i].path, ent->d_name) == 0) {
                    is_tracked = 1;
                    break;
                }
            }

            if (!is_tracked) {
                struct stat st;
                stat(ent->d_name, &st);
                if (S_ISREG(st.st_mode)) {
                    printf("  untracked:  %s\n", ent->d_name);
                    untracked_count++;
                }
            }
        }
        closedir(dir);
    }
    if (untracked_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    return 0;
}

// ================= IMPLEMENTATION =================

// LOAD INDEX
int index_load(Index *index) {
    index->count = 0;

    FILE *fp = fopen(INDEX_FILE, "r");
    if (!fp) return 0;

    while (!feof(fp)) {
        IndexEntry entry;
        char hash_hex[65];

        if (fscanf(fp, "%o %64s %ld %ld %s\n",
                   &entry.mode,
                   hash_hex,
                   &entry.mtime_sec,
                   &entry.size,
                   entry.path) != 5)
            break;

        hex_to_hash(hash_hex, &entry.hash);
        index->entries[index->count++] = entry;
    }

    fclose(fp);
    return 0;
}

// SAVE INDEX
int index_save(const Index *index) {

    FILE *fp = fopen(".pes/index.tmp", "w");
    if (!fp) return -1;

    for (int i = 0; i < index->count; i++) {
        char hex[65];
        hash_to_hex(&index->entries[i].hash, hex);

        fprintf(fp, "%o %s %ld %ld %s\n",
                index->entries[i].mode,
                hex,
                index->entries[i].mtime_sec,
                index->entries[i].size,
                index->entries[i].path);
    }

    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    rename(".pes/index.tmp", INDEX_FILE);
    return 0;
}

// ADD FILE
int index_add(Index *index, const char *path) {

    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    rewind(fp);

    void *buffer = malloc(size);
    fread(buffer, 1, size, fp);
    fclose(fp);

    ObjectID hash;
    if (object_write(OBJ_BLOB, buffer, size, &hash) != 0) {
        free(buffer);
        return -1;
    }

    free(buffer);

    struct stat st;
    stat(path, &st);

    IndexEntry *existing = index_find(index, path);

    if (existing) {
        existing->hash = hash;
        existing->mtime_sec = st.st_mtime;
        existing->size = st.st_size;
        existing->mode = st.st_mode;
    } else {
        IndexEntry *entry = &index->entries[index->count++];
        entry->hash = hash;
        entry->mtime_sec = st.st_mtime;
        entry->size = st.st_size;
        entry->mode = st.st_mode;
        strcpy(entry->path, path);
    }

    return index_save(index);
}
