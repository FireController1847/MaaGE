#include "registry.h"
#include "files.h"
#include "semver.h"
#include "maage/log.h"
#include "../access/verify.h"
#include <json-c/json.h>
#include <openssl/evp.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#define MAAGE_CLIENT_PAYLOAD "package"
#define MAAGE_CLIENT_DESCRIPTOR "maage-client.json"
#ifdef _WIN32
#define MAAGE_CLIENT_FILENAME "maage-client.exe"
#else
#define MAAGE_CLIENT_FILENAME "maage-client"
#endif
static int maage_client_text(struct json_object *o, const char *key, char *out, size_t capacity, int optional)
{
    const char *s = maage_access_json_string(o, key), *p;
    if ((!optional && !*s) || strlen(s) >= capacity) return 0;
    for (p = s; *p; ++p) if ((unsigned char)*p < 32 || (unsigned char)*p > 126 || *p == '"' || *p == '\\') return 0;
    strcpy(out, s); return 1;
}
static int maage_client_id(const char *s)
{ return strlen(s) == 64 && strspn(s, "0123456789abcdef") == 64; }
static struct json_object *maage_client_json(const char *path)
{
    char *data = (char *)malloc(262145);
    FILE *f;
    size_t n;
    struct json_object *o = NULL;
    if (!data) return NULL;
    f = maage_storage_open(path, "rb");
    if (f) { n = fread(data, 1, 262145, f); if (!ferror(f) && n < 262145) o = maage_access_json(data, n); fclose(f); }
    free(data); return o;
}
static int maage_client_fields(struct json_object *o, maage_client_build *b)
{
    return maage_client_text(o,"version",b->version,sizeof(b->version),0) && maage_semver_valid(b->version) &&
        maage_client_text(o,"build",b->build,sizeof(b->build),1) &&
        maage_client_text(o,"build_type",b->build_type,sizeof(b->build_type),0) &&
        maage_client_text(o,"platform",b->platform,sizeof(b->platform),0) &&
        maage_client_text(o,"architecture",b->architecture,sizeof(b->architecture),0) &&
        maage_client_text(o,"executable",b->executable,sizeof(b->executable),0) &&
        strspn(b->executable,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") == strlen(b->executable) &&
        strcmp(b->executable,".") && strcmp(b->executable,"..") &&
        maage_client_text(o,"sha256",b->sha256,sizeof(b->sha256),0) && maage_client_id(b->sha256) &&
        maage_client_text(o,"channel",b->channel,sizeof(b->channel),1);
}
static int maage_client_executable(const char *package, const maage_client_build *b, char path[MAAGE_PATH_CAPACITY])
{
    return maage_storage_directory_exists(package) && maage_storage_join(path,MAAGE_PATH_CAPACITY,package,b->executable);
}
static int maage_client_identity(maage_client_build *b)
{
    char identity[1024]; unsigned char digest[32]; unsigned n, i;
    sprintf(identity,"%s\n%s\n%s\n%s\n%s\n%s",b->version,b->build,b->platform,b->architecture,b->build_type,b->sha256);
    if (EVP_Digest(identity,strlen(identity),digest,&n,EVP_sha256(),NULL) != 1 || n != 32) return 0;
    for (i=0;i<32;++i) sprintf(b->id+i*2,"%02x",digest[i]);
    return 1;
}
static int maage_client_compatible(const maage_client_build *b)
{
    const char *p = b->architecture, *end, *host, *host_end;
    if (strcmp(b->platform,MAAGE_CLIENT_HOST_PLATFORM)) return 0;
    do {
        end = strchr(p,','); if (!end) end = p + strlen(p);
        host = MAAGE_CLIENT_HOST_ARCH;
        do {
            host_end = strchr(host,','); if (!host_end) host_end = host + strlen(host);
            if (end-p == host_end-host && !memcmp(p,host,(size_t)(end-p))) return 1;
            host = *host_end ? host_end+1 : host_end;
        } while (*host);
        p = *end ? end+1 : end;
    } while (*p);
    return 0;
}
static int maage_client_inspect(const char *package, maage_client_build *b, char error[256])
{
    char path[MAAGE_PATH_CAPACITY], actual[65];
    struct json_object *o, *v;
    FILE *f;
    unsigned char magic[4];
    int ok;
    if (!maage_storage_directory_exists(package) || !maage_storage_join(path,sizeof(path),package,MAAGE_CLIENT_DESCRIPTOR)) goto failed;
    o = maage_client_json(path);
    ok = o && json_object_object_get_ex(o,"schema",&v) && json_object_get_int(v)==1 &&
        !strcmp(maage_access_json_string(o,"application"),"maage-client") && maage_client_fields(o,b) && !strcmp(b->executable,MAAGE_CLIENT_FILENAME);
    /* No network protocol has been implemented; do not claim compatibility. */
    if (ok && json_object_object_get_ex(o,"protocol",&v) && v) ok = json_object_is_type(v,json_type_null);
    if (o) json_object_put(o);
    if (!ok || !maage_client_compatible(b)) { strcpy(error,"Select a MaaGE Client package for this platform and architecture with a valid schema-1 manifest. Network protocol must be undefined."); return 0; }
    if (!maage_client_executable(package,b,path) || !maage_client_executable_file(path) || !(f=maage_storage_open(path,"rb"))) goto failed;
    ok = fread(magic,1,4,f)==4; fclose(f);
#ifdef __APPLE__
    ok = ok && ((!memcmp(magic,"\xcf\xfa\xed\xfe",4)) || (!memcmp(magic,"\xfe\xed\xfa\xcf",4)) || (!memcmp(magic,"\xca\xfe\xba\xbe",4)) || (!memcmp(magic,"\xca\xfe\xba\xbf",4)));
#elif defined(_WIN32)
    ok = ok && magic[0]=='M' && magic[1]=='Z';
#else
    ok = ok && !memcmp(magic,"\177ELF",4);
#endif
    if (!ok || !maage_client_sha256(path,actual) || strcmp(actual,b->sha256) || !maage_client_identity(b)) goto failed;
    b->available = 1; strcpy(b->status,"Available"); return 1;
failed:
    strcpy(error,"Client package is missing, unsafe, incomplete, or its executable does not match the build manifest. Rebuild and import the whole package."); return 0;
}
int maage_clients_find(const maage_client_registry *r, const char *id)
{
    unsigned i; for(i=0;i<r->count;++i) if (!strcmp(r->builds[i].id,id)) return (int)i;
    return -1;
}
static int maage_client_directory(const maage_client_registry *r, const char *id, char path[MAAGE_PATH_CAPACITY])
{
    char relative[96];
    if (!maage_client_id(id)) return 0;
    sprintf(relative,"clients/%s",id); return maage_storage_join(path,MAAGE_PATH_CAPACITY,r->root,relative);
}
static int maage_client_package(const maage_client_registry *r, const char *id, char path[MAAGE_PATH_CAPACITY])
{ return maage_client_directory(r,id,path) && maage_storage_join(path,MAAGE_PATH_CAPACITY,path,MAAGE_CLIENT_PAYLOAD); }
int maage_clients_path(const maage_client_registry *r, const char *id, char path[MAAGE_PATH_CAPACITY])
{
    int index=maage_clients_find(r,id);
    char package[MAAGE_PATH_CAPACITY];
    return index>=0 && r->builds[index].available && maage_client_package(r,id,package) && maage_client_executable(package,&r->builds[index],path) && maage_client_executable_file(path);
}
void maage_client_build_label(const maage_client_build *b, char label[384])
{ sprintf(label,"%s%s%s | %.24s | %.12s | %.96s",b->version,*b->build ? " + " : "",b->build,b->build_type,b->origin,b->status); }
static int maage_client_save(maage_client_registry *r)
{
    struct json_object *o=json_object_new_object(), *list=json_object_new_array(), *b;
    char path[MAAGE_PATH_CAPACITY], tmp[MAAGE_PATH_CAPACITY];
    const char *text; unsigned i; FILE *f; int ok=0;
    json_object_object_add(o,"schema",json_object_new_int(1));
    for(i=0;i<r->count;++i) {
        maage_client_build *item=&r->builds[i]; b=json_object_new_object();
#define MAAGE_CLIENT_FIELD(key) json_object_object_add(b,#key,json_object_new_string(item->key))
        MAAGE_CLIENT_FIELD(id); MAAGE_CLIENT_FIELD(version); MAAGE_CLIENT_FIELD(build); MAAGE_CLIENT_FIELD(build_type);
        MAAGE_CLIENT_FIELD(platform); MAAGE_CLIENT_FIELD(architecture); MAAGE_CLIENT_FIELD(executable);
        MAAGE_CLIENT_FIELD(sha256); MAAGE_CLIENT_FIELD(origin); MAAGE_CLIENT_FIELD(channel);
#undef MAAGE_CLIENT_FIELD
        json_object_object_add(b,"imported",json_object_new_double(item->imported)); json_object_array_add(list,b);
    }
    json_object_object_add(o,"builds",list); text=json_object_to_json_string_ext(o,JSON_C_TO_STRING_PRETTY);
    if (maage_storage_join(path,sizeof(path),r->root,"config/client-builds.json") &&
        maage_storage_join(tmp,sizeof(tmp),r->root,"config/client-builds.json.tmp") && (f=maage_storage_open(tmp,"wb"))!=NULL) {
        ok=fwrite(text,1,strlen(text),f)==strlen(text); if(fclose(f)) ok=0;
        if(ok) ok=maage_storage_replace(tmp,path);
        if(!ok) maage_storage_remove(tmp);
    }
    json_object_put(o); return ok;
}
int maage_clients_load(maage_client_registry *r, const char *root, char error[256])
{
    char path[MAAGE_PATH_CAPACITY], why[256];
    struct json_object *o, *list, *b, *v;
    unsigned i,j;
    maage_client_build candidate;
    memset(r,0,sizeof(*r));
    if(strlen(root)>=sizeof(r->root)) return 0;
    strcpy(r->root,root);
    if(!maage_storage_join(path,sizeof(path),root,"config") || !maage_storage_directories(path) ||
        !maage_storage_join(path,sizeof(path),root,"config/client-builds.json")) return 0;
    if(!maage_client_file_kind(path)) return 1;
    o=maage_client_json(path);
    if(!o || !json_object_object_get_ex(o,"schema",&v) || json_object_get_int(v)!=1 ||
        !json_object_object_get_ex(o,"builds",&list) || !json_object_is_type(list,json_type_array) || json_object_array_length(list)>MAAGE_CLIENT_BUILDS) {
        if(o) json_object_put(o); strcpy(error,"Cannot read Client registry. Restore config/client-builds.json before managing builds."); return 0;
    }
    for(i=0;i<json_object_array_length(list);++i) {
        maage_client_build *item=&r->builds[r->count];
        b=json_object_array_get_idx(list,i); memset(item,0,sizeof(*item));
        if(!maage_client_fields(b,item) || !maage_client_text(b,"id",item->id,sizeof(item->id),0) || !maage_client_id(item->id) ||
            !maage_client_text(b,"origin",item->origin,sizeof(item->origin),0) || (strcmp(item->origin,"local") && strcmp(item->origin,"remote")) || maage_clients_find(r,item->id)>=0) goto invalid;
        candidate=*item;
        if(!maage_client_identity(&candidate) || strcmp(candidate.id,item->id)) goto invalid;
        if(json_object_object_get_ex(b,"imported",&v)) item->imported=json_object_get_double(v);
        memset(&candidate,0,sizeof(candidate));
        if(maage_client_package(r,item->id,path) && maage_client_inspect(path,&candidate,why) && !strcmp(item->id,candidate.id)) {
            item->available=1; strcpy(item->status,"Available");
        } else strcpy(item->status,maage_client_compatible(item) ? "Missing/legacy package; import standalone Client" : "Incompatible platform or architecture");
        ++r->count;
    }
    /* Highest SemVer first; build metadata never changes SemVer precedence. */
    for(i=0;i<r->count;++i) for(j=i+1;j<r->count;++j) if(maage_semver_compare(r->builds[i].version,r->builds[j].version)<0) {
        candidate=r->builds[i]; r->builds[i]=r->builds[j]; r->builds[j]=candidate;
    }
    json_object_put(o); return 1;
invalid:
    json_object_put(o); r->count=0; strcpy(error,"Invalid Client registry entry; restore the registry before managing builds."); return 0;
}
static maage_storage_lock *maage_client_lock(maage_client_registry *r)
{
    char path[MAAGE_PATH_CAPACITY];
    return maage_storage_join(path,sizeof(path),r->root,"config/client-builds.lock") ? maage_storage_acquire(path) : NULL;
}
int maage_clients_remove(maage_client_registry *r, const char *id, char error[256])
{
    maage_storage_lock *lock=maage_client_lock(r);
    maage_client_registry *current=(maage_client_registry *)malloc(sizeof(*current));
    char directory[MAAGE_PATH_CAPACITY]; int index,ok=0;
    if(!lock || !current) goto done;
    if(!maage_clients_load(current,r->root,error)) goto done;
    index=maage_clients_find(current,id);
    if(index<0 || strcmp(current->builds[index].origin,"local") || !maage_client_directory(current,id,directory)) goto done;
    --current->count; memmove(&current->builds[index],&current->builds[index+1],(current->count-(unsigned)index)*sizeof(current->builds[0]));
    if(!maage_client_save(current)) goto done;
    *r=*current; ok=1;
    if(maage_client_file_kind(directory) && !maage_client_remove_tree(directory)) {
        strcpy(error,"Client unregistered; some package files could not be removed. Shared Minecraft resources are unchanged.");
        maage_log_warn("maage.launcher.clients","Client removed from registry; package cleanup incomplete.");
    }
 done:
    if(!ok && !*error) strcpy(error,"Cannot remove Client: registry busy, build absent, or storage unavailable.");
    free(current); if(lock) maage_storage_release(lock); return ok;
}
#if MAAGE_LAUNCHER_DEVELOPMENT
static int maage_client_nested(const char *parent, const char *path)
{
    size_t n = strlen(parent);
#ifdef _WIN32
    if (_strnicmp(parent,path,n)) return 0;
#else
    if (strncmp(parent,path,n)) return 0;
#endif
    return !path[n] || path[n]=='/';
}
int maage_clients_probe(const char *source, maage_client_candidate *c, char error[256])
{
    char *slash;
    memset(c,0,sizeof(*c));
    if(!maage_client_absolute(source,c->source)) { strcpy(error,"Cannot resolve the selected Client package."); return 0; }
    if(maage_client_file_kind(c->source)!=1 || !(slash=strrchr(c->source,'/')) || strcmp(slash+1,MAAGE_CLIENT_FILENAME)) {
        strcpy(error,"Select the standalone " MAAGE_CLIENT_FILENAME " executable with its adjacent maage-client.json manifest."); return 0;
    }
    *slash=0;
    return maage_client_inspect(c->source,&c->build,error);
}
int maage_clients_import(maage_client_registry *r, const maage_client_candidate *c, char error[256])
{
    maage_storage_lock *lock=maage_client_lock(r);
    maage_client_registry *current=(maage_client_registry *)malloc(sizeof(*current));
    maage_client_build verified;
    char staging[MAAGE_PATH_CAPACITY], destination[MAAGE_PATH_CAPACITY], package[MAAGE_PATH_CAPACITY], managed[MAAGE_PATH_CAPACITY];
    int ok=0, promoted=0, staging_owned=0;
    staging[0]=0; memset(&verified,0,sizeof(verified));
    if(!lock || !current || !maage_clients_load(current,r->root,error)) goto done;
    if(current->count==MAAGE_CLIENT_BUILDS) { strcpy(error,"Client registry is full. Remove an unused build first."); goto done; }
    if(!maage_client_inspect(c->source,&verified,error) || strcmp(verified.id,c->build.id)) { strcpy(error,"Client changed since confirmation. Inspect and confirm it again."); goto done; }
    if(maage_clients_find(current,verified.id)>=0) { strcpy(error,"This exact Client build is already registered. Select it, or remove it before importing a replacement."); goto done; }
    if(!maage_storage_join(staging,sizeof(staging),r->root,"clients") || !maage_storage_directories(staging) ||
        !maage_client_directory(current,verified.id,destination)) goto done;
    /* A stale unregistered package must never be silently overwritten. */
    if(maage_client_file_kind(destination)) { strcpy(error,"An unregistered package already occupies this build directory. Resolve it before retrying import."); goto done; }
    if(!maage_client_absolute(staging,managed)) goto done;
    if(maage_client_nested(c->source,managed) || maage_client_nested(managed,c->source)) {
        strcpy(error,"Import a developer package outside MaaGE managed storage. Source and destination trees must be separate."); goto done;
    }
    if(!maage_storage_join(staging,sizeof(staging),r->root,"clients/.import")) goto done;
    if(maage_client_file_kind(staging) && !maage_client_remove_tree(staging)) goto done;
    staging_owned=1;
    if(!maage_storage_directories(staging) || !maage_storage_join(package,sizeof(package),staging,MAAGE_CLIENT_PAYLOAD) ||
        !maage_client_copy_tree(c->source,package)) { strcpy(error,"Cannot copy Client package. Escaping links, reparse points, special files, oversized trees and unreadable files are unsupported; check package permissions."); goto done; }
    memset(&verified,0,sizeof(verified));
    if(!maage_client_inspect(package,&verified,error) || strcmp(verified.id,c->build.id)) goto done;
    if(!maage_storage_replace(staging,destination)) goto done;
    promoted=1; verified.imported=difftime(time(NULL),(time_t)0); strcpy(verified.origin,"local");
    current->builds[current->count++]=verified;
    if(!maage_client_save(current)) goto done;
    *r=*current; ok=1; maage_log_info("maage.launcher.clients","Imported Client %s (%s); developer source preserved.",verified.version,verified.build);
 done:
    if(!ok) {
        if(staging_owned) maage_client_remove_tree(promoted ? destination : staging);
        if(!*error) strcpy(error,"Client import failed. Close other Launchers and check data-folder permissions.");
    }
    free(current); if(lock) maage_storage_release(lock); return ok;
}
#endif
