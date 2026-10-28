#include "skin_cache.h"
#include "verify.h"
#include "maage/download.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

static maage_status maage_launcher_skin_status(unsigned char code, unsigned char operation)
{ return maage_status_make(code,MAAGE_CONTEXT_RESOURCE,operation,0); }
static maage_status maage_launcher_skin_directory(const char *root, const char *uuid, char path[MAAGE_PATH_CAPACITY])
{
    char relative[64];
    if (!uuid || strlen(uuid)!=32 || strspn(uuid,"0123456789abcdef")!=32)
        return maage_launcher_skin_status(MAAGE_STATUS_INVALID_ARGUMENT,MAAGE_OPERATION_RESOLVE);
    sprintf(relative,"minecraft/skins/%s",uuid);
    return maage_storage_join(path,MAAGE_PATH_CAPACITY,root,relative);
}
maage_status maage_launcher_skin_profile(struct json_object *profile,
    const char *uuid, const char *name, maage_skin_reference *skin)
{
    struct json_object *skins, *entry;
    maage_skin_reference parsed;
    char normalized[33];
    static const char secure[]="https://textures.minecraft.net/texture/";
    static const char old_scheme[]="http://textures.minecraft.net/texture/";
    const char *url, *texture, *variant, *state;
    size_t i;
    int active = 0;
    memset(&parsed,0,sizeof(parsed));
    if (!maage_access_uuid(maage_access_json_string(profile,"id"),normalized) || strcmp(normalized,uuid) ||
        strcmp(maage_access_json_string(profile,"name"),name) || !maage_access_username(name) ||
        !json_object_object_get_ex(profile,"skins",&skins) || !json_object_is_type(skins,json_type_array) ||
        json_object_array_length(skins)>64) goto malformed;
    strcpy(parsed.uuid,uuid); strcpy(parsed.name,name);
    for(i=0;i<json_object_array_length(skins);++i) {
        entry = json_object_array_get_idx(skins,i);
        if (!json_object_is_type(entry,json_type_object)) goto malformed;
        state=maage_access_json_string(entry,"state");
        if (!strcmp(state,"INACTIVE")) continue;
        if (strcmp(state,"ACTIVE")) goto malformed;
        if (active++) goto malformed;
        variant = maage_access_json_string(entry,"variant");
        if (!strcmp(variant,"SLIM")) parsed.model = MAAGE_SKIN_SLIM;
        else if (strcmp(variant,"CLASSIC")) goto malformed;
        /* Some official responses still spell this host with http. Accept
         * only that exact spelling/path, then construct an HTTPS request. */
        url = maage_access_json_string(entry,"url");
        if (!strncmp(url,secure,sizeof(secure)-1)) texture = url+sizeof(secure)-1;
        else if (!strncmp(url,old_scheme,sizeof(old_scheme)-1)) texture = url+sizeof(old_scheme)-1;
        else goto malformed;
        if (strlen(texture)!=64 || strspn(texture,"0123456789abcdef")!=64) goto malformed;
        strcpy(parsed.texture,texture);
    }
    if (!maage_skin_reference_valid(&parsed)) goto malformed;
    *skin = parsed; return maage_launcher_skin_status(MAAGE_STATUS_SUCCESS,MAAGE_OPERATION_PARSE);
malformed:
    return maage_launcher_skin_status(MAAGE_STATUS_MALFORMED_INPUT,MAAGE_OPERATION_PARSE);
}
maage_status maage_launcher_skin_cached(const char *root, const char *uuid,
    const char *name, maage_skin_reference *skin, maage_status_trace *trace)
{
    char directory[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY], data[2049], argument[MAAGE_SKIN_ARGUMENT_CAPACITY];
    struct json_object *json = NULL, *schema;
    maage_skin_reference parsed;
    maage_image image;
    size_t bytes;
    FILE *file;
    maage_status result;
    memset(&image,0,sizeof(image));
    result = maage_launcher_skin_directory(root,uuid,directory);
    if (maage_status_failed(result)) return result;
    result = maage_storage_join(path,sizeof(path),directory,"current.json");
    if (maage_status_failed(result)) return result;
    file = maage_storage_open(path,"rb");
    if (!file) return maage_launcher_skin_status(errno==ENOENT || errno==ENOTDIR ? MAAGE_STATUS_NOT_FOUND : MAAGE_STATUS_IO_ERROR,MAAGE_OPERATION_LOAD);
    bytes = fread(data,1,sizeof(data),file);
    result = maage_launcher_skin_status(ferror(file) || bytes==sizeof(data) ? MAAGE_STATUS_MALFORMED_INPUT : MAAGE_STATUS_SUCCESS,MAAGE_OPERATION_LOAD);
    if (fclose(file) && !maage_status_failed(result)) result = maage_launcher_skin_status(MAAGE_STATUS_IO_ERROR,MAAGE_OPERATION_CLOSE);
    if (!maage_status_failed(result)) result = maage_access_json(data,bytes,&json);
    if (!maage_status_failed(result)) {
        const char *value = maage_access_json_string(json,"appearance");
        if (strlen(value)>=sizeof(argument) || !json_object_object_get_ex(json,"schema",&schema) ||
            !json_object_is_type(schema,json_type_int) || json_object_get_int(schema)!=1 || maage_auth_number(json,"checked_at")<=0)
            result = maage_launcher_skin_status(MAAGE_STATUS_MALFORMED_INPUT,MAAGE_OPERATION_LOAD);
        else { strcpy(argument,value); result = maage_skin_reference_parse(argument,&parsed); }
    }
    if (!maage_status_failed(result) && strcmp(parsed.uuid,uuid)) result = maage_launcher_skin_status(MAAGE_STATUS_MALFORMED_INPUT,MAAGE_OPERATION_VALIDATE);
    if (!maage_status_failed(result) && *parsed.texture) {
        result = maage_skin_path(root,&parsed,path);
        if (!maage_status_failed(result)) result = maage_skin_load(path,parsed.model,&image,trace);
    }
    if (!maage_status_failed(result)) {
        if (!maage_access_username(name)) result = maage_launcher_skin_status(MAAGE_STATUS_INVALID_ARGUMENT,MAAGE_OPERATION_VALIDATE);
        else { strcpy(parsed.name,name); *skin = parsed; }
    }
    maage_image_destroy(&image); if (json) json_object_put(json);
    return result;
}
maage_status maage_launcher_skin_publish(const char *root, const maage_skin_reference *skin,
    const maage_auth_observer *observer)
{
    char directory[MAAGE_PATH_CAPACITY], path[MAAGE_PATH_CAPACITY], staged[MAAGE_PATH_CAPACITY], metadata[MAAGE_PATH_CAPACITY];
    char url[112], argument[MAAGE_SKIN_ARGUMENT_CAPACITY], download_error[512];
    maage_storage_lock *lock = NULL;
    maage_download_request request;
    maage_download_options options;
    maage_download_observer progress;
    maage_image image;
    FILE *file;
    maage_status result;
    int written;
    memset(&image,0,sizeof(image)); staged[0]=0;
    if (!root || !observer || !observer->cancelled || !maage_skin_reference_valid(skin)) return maage_launcher_skin_status(MAAGE_STATUS_INVALID_ARGUMENT,MAAGE_OPERATION_SAVE);
    result = maage_launcher_skin_directory(root,skin->uuid,directory);
    if (!maage_status_failed(result)) result = maage_storage_directories(directory);
    if (!maage_status_failed(result)) result = maage_storage_join(path,sizeof(path),directory,"cache.lock");
    if (!maage_status_failed(result)) result = maage_storage_acquire(path,&lock,observer->trace);
    if (maage_status_failed(result)) goto done;
    if (*skin->texture) {
        result = maage_skin_path(root,skin,path);
        if (maage_status_failed(result)) goto done;
        result = maage_skin_load(path,skin->model,&image,NULL);
        maage_image_destroy(&image);
        if (maage_status_failed(result)) {
            result = maage_storage_join(staged,sizeof(staged),directory,"download.png");
            if (maage_status_failed(result)) goto done;
            sprintf(url,"https://textures.minecraft.net/texture/%s",skin->texture);
            memset(&request,0,sizeof(request)); request.url=url; request.destination=staged;
            request.size=-1; request.limit=MAAGE_SKIN_MAX_BYTES;
            progress.cancelled=observer->cancelled; progress.bytes=NULL; progress.context=observer->context;
            maage_download_options_init(&options); options.max_concurrent=1; options.max_attempts=1; options.private_diagnostics=1;
            options.connect_timeout_seconds=3; options.timeout_seconds=8;
            options.low_speed_bytes_per_second=128; options.low_speed_seconds=4;
            result=maage_download_batch_with_options(&request,1,&progress,&options,download_error,observer->trace);
            if (!maage_status_failed(result)) result=maage_skin_load(staged,skin->model,&image,observer->trace);
            maage_image_destroy(&image);
            if (!maage_status_failed(result) && observer->cancelled(observer->context)) result=maage_launcher_skin_status(MAAGE_STATUS_INTERRUPTED,MAAGE_OPERATION_SAVE);
            if (!maage_status_failed(result)) result=maage_storage_replace(staged,path);
            if (maage_status_failed(result)) goto done;
        }
    }
    if (observer->cancelled(observer->context)) { result=maage_launcher_skin_status(MAAGE_STATUS_INTERRUPTED,MAAGE_OPERATION_SAVE); goto done; }
    result=maage_skin_reference_format(skin,argument);
    if (!maage_status_failed(result)) result=maage_storage_join(metadata,sizeof(metadata),directory,"current.json");
    if (!maage_status_failed(result)) result=maage_storage_join(staged,sizeof(staged),directory,"current.part");
    if (maage_status_failed(result)) goto done;
    file=maage_storage_open(staged,"wb");
    if (!file) { result=maage_status_from_errno(errno,MAAGE_CONTEXT_STORAGE,MAAGE_OPERATION_OPEN); goto done; }
    written=fprintf(file,"{\"schema\":1,\"appearance\":\"%s\",\"checked_at\":%.0f}\n",argument,difftime(time(NULL),(time_t)0));
    result=written>0 ? maage_storage_sync(file) : maage_launcher_skin_status(MAAGE_STATUS_IO_ERROR,MAAGE_OPERATION_WRITE);
    if (fclose(file) && !maage_status_failed(result)) result=maage_launcher_skin_status(MAAGE_STATUS_IO_ERROR,MAAGE_OPERATION_CLOSE);
    if (!maage_status_failed(result) && observer->cancelled(observer->context)) result=maage_launcher_skin_status(MAAGE_STATUS_INTERRUPTED,MAAGE_OPERATION_SAVE);
    if (!maage_status_failed(result)) result=maage_storage_replace(staged,metadata);
done:
    if (*staged) maage_storage_remove(staged);
    maage_storage_release(lock); maage_image_destroy(&image);
    return MAAGE_STATUS_PROPAGATE(observer->trace,result,MAAGE_CONTEXT_RESOURCE,MAAGE_OPERATION_SAVE);
}
maage_status maage_launcher_skin_restore(const char *root, const maage_profile_session *session,
    const maage_auth_observer *observer)
{
    maage_skin_reference skin;
    struct json_object *profile=NULL;
    char detail[256];
    long status;
    maage_status result;
    /* Reusing a current encrypted access lease also reuses its cached skin.
     * The next normal authenticated verification refreshes both metadata sets. */
    result=maage_launcher_skin_cached(root,session->uuid,session->name,&skin,NULL);
    if (!maage_status_failed(result)) return maage_launcher_skin_status(MAAGE_STATUS_NOOP,MAAGE_OPERATION_LOAD);
    result=maage_auth_request_timed(5,"https://api.minecraftservices.com/minecraft/profile",session->minecraft_token,NULL,0,observer,&status,detail,&profile);
    if (!maage_status_failed(result)) result=maage_launcher_skin_profile(profile,session->uuid,session->name,&skin);
    maage_auth_json_free(profile);
    if (!maage_status_failed(result)) result=maage_launcher_skin_publish(root,&skin,observer);
    return result;
}
