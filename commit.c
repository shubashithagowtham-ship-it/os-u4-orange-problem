// commit.c — Commit creation and history traversal

#include "commit.h"
#include "index.h"
#include "tree.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

// Forward declarations
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out);

// ─── PROVIDED ─────────────────────────────────────────

// Parse commit
int commit_parse(const void *data, size_t len, Commit *commit_out) {
    (void)len;
    const char *p = (const char *)data;
    char hex[HASH_HEX_SIZE + 1];

    if (sscanf(p, "tree %64s\n", hex) != 1) return -1;
    if (hex_to_hash(hex, &commit_out->tree) != 0) return -1;
    p = strchr(p, '\n') + 1;

    if (strncmp(p, "parent ", 7) == 0) {
        if (sscanf(p, "parent %64s\n", hex) != 1) return -1;
        if (hex_to_hash(hex, &commit_out->parent) != 0) return -1;
        commit_out->has_parent = 1;
        p = strchr(p, '\n') + 1;
    } else {
        commit_out->has_parent = 0;
    }

    char author_buf[256];
    uint64_t ts;
    if (sscanf(p, "author %255[^\n]\n", author_buf) != 1) return -1;

    char *last_space = strrchr(author_buf, ' ');
    ts = strtoull(last_space + 1, NULL, 10);
    *last_space = '\0';

    snprintf(commit_out->author, sizeof(commit_out->author), "%s", author_buf);
    commit_out->timestamp = ts;

    p = strchr(p, '\n') + 1;
    p = strchr(p, '\n') + 1;
    p = strchr(p, '\n') + 1;

    snprintf(commit_out->message, sizeof(commit_out->message), "%s", p);
    return 0;
}

// Serialize commit
int commit_serialize(const Commit *commit, void **data_out, size_t *len_out) {
    char tree_hex[65];
    char parent_hex[65];

    hash_to_hex(&commit->tree, tree_hex);

    char buf[8192];
    int n = 0;

    n += snprintf(buf + n, sizeof(buf) - n, "tree %s\n", tree_hex);

    if (commit->has_parent) {
        hash_to_hex(&commit->parent, parent_hex);
        n += snprintf(buf + n, sizeof(buf) - n, "parent %s\n", parent_hex);
    }

    n += snprintf(buf + n, sizeof(buf) - n,
        "author %s %llu\n"
        "committer %s %llu\n"
        "\n"
        "%s",
        commit->author, commit->timestamp,
        commit->author, commit->timestamp,
        commit->message);

    *data_out = malloc(n + 1);
    memcpy(*data_out, buf, n + 1);
    *len_out = n;
    return 0;
}

// Walk commits
int commit_walk(commit_walk_fn callback, void *ctx) {
    ObjectID id;
    if (head_read(&id) != 0) return -1;

    while (1) {
        ObjectType type;
        void *raw;
        size_t raw_len;

        if (object_read(&id, &type, &raw, &raw_len) != 0) return -1;

        Commit c;
        commit_parse(raw, raw_len, &c);
        free(raw);

        callback(&id, &c, ctx);

        if (!c.has_parent) break;
        id = c.parent;
    }

    return 0;
}

// Read HEAD
int head_read(ObjectID *id_out) {
    FILE *f = fopen(".pes/HEAD", "r");
    if (!f) return -1;

    char line[512];
    fgets(line, sizeof(line), f);
    fclose(f);

    line[strcspn(line, "\n")] = '\0';

    char path[512];
    if (strncmp(line, "ref: ", 5) == 0) {
        snprintf(path, sizeof(path), ".pes/%s", line + 5);
        f = fopen(path, "r");
        if (!f) return -1;
        fgets(line, sizeof(line), f);
        fclose(f);
    }

    line[strcspn(line, "\n")] = '\0';
    return hex_to_hash(line, id_out);
}

// Update HEAD
int head_update(const ObjectID *new_commit) {
    FILE *f = fopen(".pes/HEAD", "r");
    if (!f) return -1;

    char line[512];
    fgets(line, sizeof(line), f);
    fclose(f);

    line[strcspn(line, "\n")] = '\0';

    char path[512];
    if (strncmp(line, "ref: ", 5) == 0)
        snprintf(path, sizeof(path), ".pes/%s", line + 5);
    else
        snprintf(path, sizeof(path), ".pes/HEAD");

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    f = fopen(tmp, "w");
    char hex[65];
    hash_to_hex(new_commit, hex);
    fprintf(f, "%s\n", hex);

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    return rename(tmp, path);
}

// ─── IMPLEMENTATION ─────────────────────────────────

int commit_create(const char *message, ObjectID *commit_id_out) {

    ObjectID tree_id;
    if (tree_from_index(&tree_id) != 0) return -1;

    Commit commit;
    memset(&commit, 0, sizeof(commit));

    commit.tree = tree_id;

    ObjectID parent_id;
    if (head_read(&parent_id) == 0) {
        commit.parent = parent_id;
        commit.has_parent = 1;
    }

    snprintf(commit.author, sizeof(commit.author), "%s", pes_author());
    commit.timestamp = time(NULL);

    snprintf(commit.message, sizeof(commit.message), "%s", message);

    void *data;
    size_t len;

    commit_serialize(&commit, &data, &len);

    if (object_write(OBJ_COMMIT, data, len, commit_id_out) != 0) {
        free(data);
        return -1;
    }

    free(data);

    if (head_update(commit_id_out) != 0) return -1;

    return 0;
}
