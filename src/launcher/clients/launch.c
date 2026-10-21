#include "launch.h"
#include "maage/log.h"
#include "maage/instance.h"
#include <stdlib.h>
#include <string.h>
int maage_client_launch(const char *root, const char *id, const char *instance_id, maage_access *access,
    maage_process **process, char error[256])
{
    maage_client_registry *registry=(maage_client_registry *)calloc(1,sizeof(*registry));
    char executable[MAAGE_PATH_CAPACITY], directory[MAAGE_PATH_CAPACITY], logs[MAAGE_PATH_CAPACITY];
    char log[MAAGE_PATH_CAPACITY], data_argument[MAAGE_PATH_CAPACITY+18], instance_argument[48];
    const char *argv[4];
    maage_instance *instance=NULL;
    int ok=0;
    *process=NULL;
    if(!access || !maage_access_allowed(access)) { strcpy(error,"Verify the selected Minecraft account before starting a Client."); goto done; }
    if(!registry || !maage_clients_load(registry,root,error) || !maage_clients_path(registry,id,executable)) {
        if(!*error) strcpy(error,"The selected Client is missing, modified or incompatible. Import/select a valid standalone Client."); goto done;
    }
    if(!maage_instance_open(root,instance_id,0,&instance,error)) goto done;
    if(strcmp(maage_instance_get_info(instance)->client_build,id)) {
        strcpy(error,"Instance Client assignment changed. Select the instance again before launching."); goto done;
    }
    strcpy(directory,maage_instance_directory(instance));
    if(!maage_instance_resolve_path(instance,MAAGE_INSTANCE_FOLDER_LOGS,NULL,logs,sizeof(logs)) ||
        !maage_storage_directories(logs) || !maage_storage_join(log,sizeof(log),logs,"client.log") ||
        !maage_access_allowed(access)) goto done;
    sprintf(data_argument,"--data-directory=%s",root);
    sprintf(instance_argument,"--instance=%s",instance_id);
    argv[0]=executable; argv[1]=data_argument; argv[2]=instance_argument; argv[3]=NULL;
    /* Core logging adopts this live file and archives it at Client shutdown.
     * Append preserves startup errors and interrupted-session output. */
    ok=maage_process_start(executable,argv,directory,log,log,process,error);
    if(ok) maage_log_info("maage.launcher.clients","Starting selected Client %.12s directly; combined live log=%s.",id,log);
done:
    if(!ok && !*error) strcpy(error,"Cannot start Client. Check current Minecraft access and permissions for the Client and logs folders.");
    if(!ok) maage_log_error("maage.launcher.clients","%s",error);
    maage_instance_close(instance);
    maage_clients_dispose(registry); free(registry); return ok;
}
