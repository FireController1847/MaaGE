#include "launch.h"
#include "files.h"
#include "maage/log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
int maage_client_launch(const char *root, const char *id, maage_access *access,
    maage_process **process, char error[256])
{
    maage_client_registry *registry=(maage_client_registry *)malloc(sizeof(*registry));
    char executable[MAAGE_PATH_CAPACITY], directory[MAAGE_PATH_CAPACITY], logs[MAAGE_PATH_CAPACITY];
    char output[MAAGE_PATH_CAPACITY], errors[MAAGE_PATH_CAPACITY], log[MAAGE_PATH_CAPACITY], argument[MAAGE_PATH_CAPACITY+12], data_argument[MAAGE_PATH_CAPACITY+18], name[128];
    const char *argv[5];
    char *slash;
    maage_storage_lock *lock=NULL;
    unsigned attempt;
    double now=difftime(time(NULL),(time_t)0);
    int ok=0;
    *process=NULL;
    if(!access || !maage_access_allowed(access)) { strcpy(error,"Verify the selected Minecraft account before starting a Client."); goto done; }
    if(!registry || !maage_clients_load(registry,root,error) || !maage_clients_path(registry,id,executable)) {
        if(!*error) strcpy(error,"The selected Client is missing, modified or incompatible. Import/select a valid standalone Client."); goto done;
    }
    strcpy(directory,executable); slash=strrchr(directory,'/'); if(!slash) goto done; *slash=0;
    if(!maage_storage_join(logs,sizeof(logs),root,"logs/clients") || !maage_storage_directories(logs) ||
        !maage_storage_join(output,sizeof(output),logs,"launch.lock") || !(lock=maage_storage_acquire(output))) goto done;
    for(attempt=0;attempt<1000;++attempt) {
        sprintf(name,"client-%.12s-%.0f-%u.stdout.log",id,now,attempt);
        if(!maage_storage_join(output,sizeof(output),logs,name)) goto done;
        sprintf(name,"client-%.12s-%.0f-%u.stderr.log",id,now,attempt);
        if(!maage_storage_join(errors,sizeof(errors),logs,name)) goto done;
        sprintf(name,"client-%.12s-%.0f-%u.log",id,now,attempt);
        if(!maage_storage_join(log,sizeof(log),logs,name)) goto done;
        if(!maage_client_file_kind(output) && !maage_client_file_kind(errors) && !maage_client_file_kind(log)) break;
    }
    if(attempt==1000 || !maage_access_allowed(access)) goto done;
    sprintf(argument,"--log-file=%s",log);
    sprintf(data_argument,"--data-directory=%s",root);
    argv[0]=executable; argv[1]=argument; argv[2]="--log-console=on"; argv[3]=data_argument; argv[4]=NULL;
    ok=maage_process_start(executable,argv,directory,output,errors,process,error);
    if(ok) maage_log_info("maage.launcher.clients","Starting selected Client %.12s directly; stdout=%s; stderr=%s.",id,output,errors);
done:
    if(!ok && !*error) strcpy(error,"Cannot start Client. Check current Minecraft access and permissions for the Client and logs folders.");
    if(!ok) maage_log_error("maage.launcher.clients","%s",error);
    if(lock) maage_storage_release(lock);
    free(registry); return ok;
}
