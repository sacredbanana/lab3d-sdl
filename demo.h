

typedef struct demofile demofile_t;

/* Static set of variables which define game state. */
typedef struct {
    char* name;
    void* ptr;
    int elemsize;
    int cnt;
} demo_vardef_t;

#define DEMO_VAR(t) { #t, &t, sizeof(t), 1 }
#define DEMO_ARRAY(t) { #t, &t[0], sizeof(t[0]), sizeof(t) / sizeof(t[0]) }

demofile_t* demofile_open(const char* filename, demo_vardef_t* vars, int record, int format);
void demofile_close(demofile_t*);
int demofile_rewindable(demofile_t*);
void demofile_write_frame(demofile_t*, int);
void demofile_write_event(demofile_t* d, unsigned char* data, int size);

int demofile_advance(demofile_t*, int);
void demofile_update_vars(demofile_t*);
